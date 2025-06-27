#include "../include/SharedMemoryPool.h"

SharedMemoryPool::SharedMemoryPool(const std::string& base_name, size_t pool_size)
    : base_name_(base_name), pool_size_(pool_size) {
    
    // 创建接收通道
    bool recv_owner = false;
    channels_.recv_channel = createChannel(base_name_ + "_recv", recv_owner);
    ownership_.recv_owner = recv_owner;
    
    // 创建发送通道
    bool send_owner = false;
    channels_.send_channel = createChannel(base_name_ + "_send", send_owner);
    ownership_.send_owner = send_owner;

    std::cout << "SharedMemoryPool created with channels: " 
              << base_name_ + "_recv" << " and "
              << base_name_ + "_send" << std::endl;
}

SharedMemoryPool::~SharedMemoryPool() {
    // 解除映射接收通道
    if (channels_.recv_channel) {
        munmap(channels_.recv_channel, 
               sizeof(DoubleBufferChannel) + pool_size_ * sizeof(TransportMessage));
    }
    
    // 解除映射发送通道
    if (channels_.send_channel) {
        munmap(channels_.send_channel, 
               sizeof(DoubleBufferChannel) + pool_size_ * sizeof(TransportMessage));
    }
    
    // 如果是所有者，则取消链接共享内存
    if (ownership_.recv_owner) {
        shm_unlink((base_name_ + "_recv").c_str());
    }
    if (ownership_.send_owner) {
        shm_unlink((base_name_ + "_send").c_str());
    }
}

DoubleBufferChannel* SharedMemoryPool::createChannel(const std::string& name, bool& is_owner) {
    is_owner = false;
    
    // 尝试打开现有共享内存
    int fd = shm_open(name.c_str(), O_RDWR, 0666);
    
    if (fd == -1) {
        // 不存在则创建
        fd = shm_open(name.c_str(), O_CREAT | O_RDWR, 0666);
        if (fd == -1) {
            throw std::runtime_error("Failed to create shared memory " + name + 
                                    ": " + std::string(strerror(errno)));
        }
        
        is_owner = true;
        
        // 设置共享内存大小
        size_t total_size = sizeof(DoubleBufferChannel) + pool_size_ * sizeof(TransportMessage);
        if (ftruncate(fd, total_size) == -1) {
            close(fd);
            throw std::runtime_error("Failed to set size for " + name + 
                                    ": " + std::string(strerror(errno)));
        }
    }
    
    // 映射共享内存
    void* addr = mmap(nullptr, 
                     sizeof(DoubleBufferChannel) + pool_size_ * sizeof(TransportMessage),
                     PROT_READ | PROT_WRITE, 
                     MAP_SHARED, 
                     fd, 0);
    
    if (addr == MAP_FAILED) {
        close(fd);
        throw std::runtime_error("Failed to map shared memory " + name + 
                                ": " + std::string(strerror(errno)));
    }
    
    close(fd);  // 文件描述符在映射后可以关闭
    
    DoubleBufferChannel* channel = static_cast<DoubleBufferChannel*>(addr);
    
    // 如果是所有者，则初始化通道
    if (is_owner) {
        channel->read_index.store(0);
        channel->write_index.store(0);
        channel->pool_size = pool_size_;
    }
    
    return channel;
}

bool SharedMemoryPool::writeToChannel(DoubleBufferChannel* channel, uint32_t can_id, 
                                     const std::vector<uint8_t>& data) {
    if (!channel || data.size() > sizeof(TransportMessage::data)) {
        return false;
    }
    
    size_t current_write = channel->write_index.load(std::memory_order_relaxed);
    size_t next_write = (current_write + 1) % channel->pool_size;
    
    // 检查缓冲区是否已满
    if (next_write == channel->read_index.load(std::memory_order_acquire)) {
        return false;  // 缓冲区满
    }
    
    TransportMessage& msg = channel->messages[current_write];
    msg.can_id = can_id;
    msg.timestamp = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    msg.data_length = static_cast<uint16_t>(data.size());
    memcpy(msg.data, data.data(), data.size());
    
    // 更新写索引
    channel->write_index.store(next_write, std::memory_order_release);
    return true;
}

bool SharedMemoryPool::readFromChannel(DoubleBufferChannel* channel, uint32_t& can_id, 
                                      std::vector<uint8_t>& data) {
    if (!channel) {
        return false;
    }
    
    size_t current_read = channel->read_index.load(std::memory_order_relaxed);
    
    // 检查是否有新数据
    if (current_read == channel->write_index.load(std::memory_order_acquire)) {
        return false;  // 无新数据
    }
    
    const TransportMessage& msg = channel->messages[current_read];
    can_id = msg.can_id;
    data.assign(msg.data, msg.data + msg.data_length);
    
    // 更新读索引
    size_t next_read = (current_read + 1) % channel->pool_size;
    channel->read_index.store(next_read, std::memory_order_release);
    return true;
}

// 接收通道接口
bool SharedMemoryPool::writeToReceiveChannel(uint32_t can_id, const std::vector<uint8_t>& data) {
    return writeToChannel(channels_.recv_channel, can_id, data);
}

bool SharedMemoryPool::readFromReceiveChannel(uint32_t& can_id, std::vector<uint8_t>& data) {
    return readFromChannel(channels_.recv_channel, can_id, data);
}

// 发送通道接口
bool SharedMemoryPool::writeToSendChannel(uint32_t can_id, const std::vector<uint8_t>& data) {
    return writeToChannel(channels_.send_channel, can_id, data);
}

bool SharedMemoryPool::readFromSendChannel(uint32_t& can_id, std::vector<uint8_t>& data) {
    return readFromChannel(channels_.send_channel, can_id, data);
}
