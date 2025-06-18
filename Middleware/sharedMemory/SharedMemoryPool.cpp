#include "../include/SharedMemoryPool.h"
#include <iostream>
#include <string.h>
#include <errno.h>
#include <stdio.h>
#include <chrono>

SharedMemoryPool::SharedMemoryPool(const std::string& name, size_t pool_size)
    : shm_name_(name), pool_size_(pool_size) {
    
    // 尝试打开现有共享内存
    shm_fd_ = shm_open(shm_name_.c_str(), O_RDWR, 0666);
    
    if (shm_fd_ == -1) {
        // 不存在则创建
        shm_fd_ = shm_open(shm_name_.c_str(), O_CREAT | O_RDWR, 0666);
        if (shm_fd_ == -1) {
            throw std::runtime_error("Failed to create shared memory: " + 
                                    std::string(strerror(errno)));
        }
        
        is_owner_ = true;
        if (ftruncate(shm_fd_, sizeof(DoubleBufferChannel) + pool_size * sizeof(TransportMessage)) == -1) {
            throw std::runtime_error("Failed to set shared memory size: " + 
                                    std::string(strerror(errno)));
        }
    }
    
    // 映射共享内存
    void* addr = mmap(nullptr, 
                     sizeof(DoubleBufferChannel) + pool_size * sizeof(TransportMessage),
                     PROT_READ | PROT_WRITE, 
                     MAP_SHARED, 
                     shm_fd_, 0);
    
    if (addr == MAP_FAILED) {
        throw std::runtime_error("Failed to map shared memory: " + 
                                std::string(strerror(errno)));
    }
    
    channel_ = static_cast<DoubleBufferChannel*>(addr);
    
    if (is_owner_) {
        // 初始化双缓冲通道
        channel_->read_index.store(0);
        channel_->write_index.store(0);
        channel_->pool_size = pool_size;
    }
}

SharedMemoryPool::~SharedMemoryPool() {
    if (channel_) {
        munmap(channel_, sizeof(DoubleBufferChannel) + pool_size_ * sizeof(TransportMessage));
    }
    if (shm_fd_ != -1) {
        close(shm_fd_);
    }
    if (is_owner_) {
        shm_unlink(shm_name_.c_str());
    }
}

bool SharedMemoryPool::writeMessage(uint32_t can_id, const std::vector<uint8_t>& data) {
    if (data.size() > sizeof(TransportMessage::data)) return false;
    
    size_t current_write = channel_->write_index.load(std::memory_order_relaxed);
    size_t next_write = (current_write + 1) % channel_->pool_size;
    
    // 检查缓冲区是否已满
    if (next_write == channel_->read_index.load(std::memory_order_acquire)) {
        return false;
    }
    
    TransportMessage& msg = channel_->messages[current_write];
    msg.can_id = can_id;
    msg.timestamp = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    msg.data_length = static_cast<uint16_t>(data.size());
    memcpy(msg.data, data.data(), data.size());
    
    // 更新写索引
    channel_->write_index.store(next_write, std::memory_order_release);
    return true;
}

bool SharedMemoryPool::tryReadMessage(uint32_t& can_id, std::vector<uint8_t>& data) {
    size_t current_read = channel_->read_index.load(std::memory_order_relaxed);
    
    // 检查是否有新数据
    if (current_read == channel_->write_index.load(std::memory_order_acquire)) {
        return false;
    }
    
    const TransportMessage& msg = channel_->messages[current_read];
    can_id = msg.can_id;
    data.assign(msg.data, msg.data + msg.data_length);
    
    // 更新读索引
    size_t next_read = (current_read + 1) % channel_->pool_size;
    channel_->read_index.store(next_read, std::memory_order_release);
    return true;
}

