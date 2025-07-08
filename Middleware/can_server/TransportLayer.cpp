#include "../include/TransportLayer.h"
#include <iostream>
#include <chrono>
#include <iomanip>


#define _DBUG_ 1
TransportLayer::TransportLayer(FlexCANController& controller, 
                             const std::string& shm_name,
                             size_t shm_pool_size)
    : can_controller_(controller),
      shm_pool_(new SharedMemoryPool(shm_name, shm_pool_size)) 
{
    TransportLayer* self = this;
    
    can_controller_.registerFrameHandler(
        [self](const EnhancedCANFrame& frame) {  
            self->handleReceivedFrame(frame);
        }
    );
}

TransportLayer::~TransportLayer() {
    stop();
}

void TransportLayer::start() {
    if (running_) return;
    
    running_ = true;
    
    // 启动接收处理线程
    receive_processor_thread_ = std::thread([this] {
        // 设置实时优先级
        struct sched_param param;
        param.sched_priority = sched_get_priority_max(SCHED_FIFO);
        pthread_setschedparam(pthread_self(), SCHED_FIFO, &param);
        
        // 主循环在processSendRequests中
    });
    
    // 启动发送请求处理线程
    send_processor_thread_ = std::thread(&TransportLayer::processSendRequests, this);
#if _DBUG_
    std::cout << "TransportLayer start success" << std::endl;
#endif
}

void TransportLayer::stop() {
    running_ = false;
    
    if (receive_processor_thread_.joinable()) {
        receive_processor_thread_.join();
    }
    
    if (send_processor_thread_.joinable()) {
        send_processor_thread_.join();
    }
}

void TransportLayer::registerMessageHandler(
    std::function<void(uint32_t, const std::vector<uint8_t>&)> handler) {
    std::lock_guard<std::mutex> lock(handler_mutex_);
    message_handler_ = std::move(handler);
}

bool TransportLayer::sendMessage(uint32_t can_id, const std::vector<uint8_t>& data) {
    if (data.size() > 4095) return false;
    
    auto frames = segmentMessage(data);
    for (const auto& frame_data : frames) {
        can_frame frame;
        frame.can_id = can_id;
        frame.can_dlc = frame_data.size();
        std::copy(frame_data.begin(), frame_data.end(), frame.data);
        if (!can_controller_.sendFrame(frame)) {
            return false;
        }
    }
    return true;
}

void TransportLayer::handleReceivedFrame(const EnhancedCANFrame& frame) {
    // 如果是远程帧或错误帧，直接处理不进入ISO-TP解析
    // if (frame.frame_ptr->can_rtr || frame.frame_ptr->can_err) {
    //     // 直接处理特殊帧
    //     std::cout << "Received special frame: ID=0x" << std::hex << frame.frame_ptr->can_id 
    //               << ", Type=" << (frame.frame_ptr->can_rtr ? "RTR" : "ERROR")
    //               << ", DLC=" << std::dec << frame.frame_ptr->can_dlc << std::endl;
    //     return;
    // }
    
    // 零拷贝访问帧数据
    const uint8_t* frame_data = frame.frame_ptr->data;
    size_t data_len = frame.frame_ptr->can_dlc;
    
    processFrame(frame.frame_ptr->can_id, frame_data, data_len);
}

void TransportLayer::processFrame(uint32_t can_id, const uint8_t* frame_data, size_t data_len) {
    if (data_len == 0) return;

#if _DBUG_

    // 添加帧日志
    std::cout << "Processing frame: ID=0x" << std::hex << can_id 
              << ", DLC=" << std::dec << data_len << ", Data:";
    for (size_t i = 0; i < data_len; i++) {
        std::cout << " " << std::hex << static_cast<int>(frame_data[i]);
    }
    std::cout << std::dec << std::endl;
#endif

    const FrameType type = static_cast<FrameType>(frame_data[0] >> 4);
    
    switch (type) {
    case SINGLE_FRAME:
        handleSingleFrame(can_id, frame_data, data_len);
        break;
    case FIRST_FRAME:
        handleFirstFrame(can_id, frame_data, data_len);
        break;
    case CONSECUTIVE_FRAME:
        handleConsecutiveFrame(can_id, frame_data, data_len);
        break;
    case FLOW_CONTROL_FRAME:
        // 流控帧处理 (暂时忽略)
        break;
    default:
        // 未知帧类型处理
        std::cout << "Unknown frame type: " << static_cast<int>(type) << std::endl;
        break;
    }
}

void TransportLayer::handleSingleFrame(uint32_t can_id, const uint8_t* frame_data, size_t data_len) {
    if (data_len < 1) return;

    // 单帧: 格式 [PCI | data...]
    // PCI: 高4位=0(帧类型), 低4位=数据长度
    const uint8_t pci = frame_data[0];
    const uint8_t length = pci & 0x0F;
    
    // 检查数据长度是否匹配
    if (data_len < 1 + length) {
        std::cerr << "Single frame length mismatch. Expected: " << (1 + length) 
                  << ", Actual: " << data_len << std::endl;
        return;
    }
    
    // 提取数据部分 (跳过PCI字节)
    std::vector<uint8_t> data;
    data.reserve(length);
    for (size_t i = 1; i < 1 + length; i++) {
        data.push_back(frame_data[i]);
    }
    
#if _DBUG_

    // 添加接收日志
    std::cout << "Received single frame: ID=0x" << std::hex << can_id 
              << ", Length=" << std::dec << data.size() << std::endl;
#endif
    
    deliverMessage(can_id, data);
}

void TransportLayer::handleFirstFrame(uint32_t can_id, const uint8_t* frame_data, size_t data_len) {
    if (data_len < 2) {
        std::cerr << "First frame too short: " << data_len << " bytes" << std::endl;
        return;
    }

    // 首帧: 格式 [PCI1 | PCI2 | data...]
    // PCI1: 高4位=1(帧类型), 低4位=长度高4位
    // PCI2: 长度低8位
    const uint8_t pci1 = frame_data[0];
    const uint8_t pci2 = frame_data[1];
    const uint16_t total_length = ((pci1 & 0x0F) << 8) | pci2;
    
    std::lock_guard<std::mutex> lock(reassembly_mutex_);
    
    // 创建重组上下文
    ReassemblyContext ctx;
    ctx.expected_can_id = can_id;
    ctx.total_length = total_length;
    ctx.next_sequence = 0;  // 下一个期望序列号从0开始
    ctx.last_received = std::chrono::steady_clock::now().time_since_epoch().count();
    
    // 添加首帧中的数据部分
    if (data_len > 2) {
        ctx.buffer.reserve(total_length);
        for (size_t i = 2; i < data_len; i++) {
            ctx.buffer.push_back(frame_data[i]);
        }
    }
    
    reassembly_contexts_[can_id] = ctx;
#if _DBUG_
    std::cout << "Received first frame: ID=0x" << std::hex << can_id 
              << ", Total length=" << std::dec << total_length 
              << ", Initial data=" << ctx.buffer.size() << " bytes" << std::endl;
#endif
}

void TransportLayer::handleConsecutiveFrame(uint32_t can_id, const uint8_t* frame_data, size_t data_len) {
    if (data_len < 1) return;
    
    std::lock_guard<std::mutex> lock(reassembly_mutex_);
    
    auto it = reassembly_contexts_.find(can_id);
    if (it == reassembly_contexts_.end()) {
        std::cerr << "Consecutive frame without context: ID=0x" << std::hex << can_id << std::endl;
        return;
    }
    
    ReassemblyContext& ctx = it->second;
    
    // 连续帧: 格式 [PCI | data...]
    // PCI: 高4位=2(帧类型), 低4位=序列号
    const uint8_t pci = frame_data[0];
    const uint8_t seq_num = pci & 0x0F;
    
    // 检查序列号是否匹配
    if (seq_num != ctx.next_sequence) {
        std::cerr << "Sequence number mismatch. Expected: " << static_cast<int>(ctx.next_sequence)
                  << ", Received: " << static_cast<int>(seq_num) << std::endl;
        reassembly_contexts_.erase(it);
        return;
    }
    
    // 添加数据部分 (跳过PCI字节)
    size_t data_added = 0;
    if (data_len > 1) {
        for (size_t i = 1; i < data_len; i++) {
            ctx.buffer.push_back(frame_data[i]);
            data_added++;
        }
    }
    
    // 更新上下文
    ctx.next_sequence = (ctx.next_sequence + 1) % 16;
    ctx.last_received = std::chrono::steady_clock::now().time_since_epoch().count();
#if _DBUG_
    std::cout << "Received consecutive frame: ID=0x" << std::hex << can_id 
              << ", Seq=" << std::dec << static_cast<int>(seq_num)
              << ", Added=" << data_added << " bytes"
              << ", Total=" << ctx.buffer.size() << "/" << ctx.total_length << std::endl;
#endif
    // 检查是否接收完成
    if (ctx.buffer.size() >= ctx.total_length) {
        std::vector<uint8_t> complete_data(
            ctx.buffer.begin(), 
            ctx.buffer.begin() + ctx.total_length
        );
        
        // 投递完整消息
        deliverMessage(ctx.expected_can_id, complete_data);
#if _DBUG_
        std::cout << "Reassembly complete: ID=0x" << std::hex << ctx.expected_can_id 
                  << ", Length=" << std::dec << complete_data.size() << std::endl;
#endif
        
        reassembly_contexts_.erase(it);
    }
}

void TransportLayer::deliverMessage(uint32_t can_id, const std::vector<uint8_t>& data) {
#if _DBUG_
    // 添加接收日志
    std::cout << "Delivering message: ID=0x" << std::hex << can_id 
              << ", Length=" << std::dec << data.size() << std::endl;
#endif
    
    // 调用消息处理器
    {
        std::lock_guard<std::mutex> lock(handler_mutex_);
        if (message_handler_) {
            message_handler_(can_id, data);
        }
    }
    
    // 写入共享内存的接收通道
    shm_pool_->writeToReceiveChannel(can_id, data);
}

void TransportLayer::cleanupExpiredContexts() {
    std::lock_guard<std::mutex> lock(reassembly_mutex_);
    auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    const uint64_t timeout_ns = 1000000000; // 1秒超时
    
    for (auto it = reassembly_contexts_.begin(); it != reassembly_contexts_.end(); ) {
        if (now - it->second.last_received > timeout_ns) {
            std::cerr << "Cleaning up expired context: ID=0x" << std::hex << it->first 
                      << ", Progress=" << std::dec << it->second.buffer.size() 
                      << "/" << it->second.total_length << std::endl;
            it = reassembly_contexts_.erase(it);
        } else {
            ++it;
        }
    }
}

std::vector<std::vector<uint8_t>> TransportLayer::segmentMessage(const std::vector<uint8_t>& data) {
    std::vector<std::vector<uint8_t>> frames;

    if (data.size() <= 7) {
        // 单帧
        std::vector<uint8_t> frame;
        frame.push_back(static_cast<uint8_t>(SINGLE_FRAME << 4) | static_cast<uint8_t>(data.size()));
        frame.insert(frame.end(), data.begin(), data.end());
        frames.push_back(frame);
#if _DBUG_

        // 添加发送日志
        std::cout << "Segmenting as single frame: Length=" << data.size() 
                  << ", Frame DLC=" << frame.size() << std::endl;
#endif
    } else {
        // 首帧
        std::vector<uint8_t> first_frame;
        first_frame.push_back(static_cast<uint8_t>(FIRST_FRAME << 4) | static_cast<uint8_t>((data.size() >> 8) & 0x0F));
        first_frame.push_back(static_cast<uint8_t>(data.size() & 0xFF));
        
        // 首帧最多携带6字节数据（PCI占2字节）
        size_t first_frame_data_len = std::min<size_t>(6, data.size());
        first_frame.insert(first_frame.end(), data.begin(), data.begin() + first_frame_data_len);
        frames.push_back(first_frame);
#if _DBUG_
        
        // 添加发送日志
        std::cout << "Segmenting as first frame: Total length=" << data.size() 
                  << ", Initial data=" << first_frame_data_len << " bytes" << std::endl;
#endif

        // 连续帧
        size_t offset = first_frame_data_len;
        uint8_t seq_num = 0; // 序列号从0开始
        while (offset < data.size()) {
            std::vector<uint8_t> frame;
            frame.push_back(static_cast<uint8_t>(CONSECUTIVE_FRAME << 4) | (seq_num & 0x0F));
            
            // 连续帧最多携带7字节数据
            size_t chunk_size = std::min<size_t>(7, data.size() - offset);
            frame.insert(frame.end(), data.begin() + offset, data.begin() + offset + chunk_size);
            frames.push_back(frame);
#if _DBUG_
            // 添加发送日志
            std::cout << "  Consecutive frame " << static_cast<int>(seq_num) 
                      << ": Data=" << chunk_size << " bytes" << std::endl;
#endif

            offset += chunk_size;
            seq_num = (seq_num + 1) % 16; // 序列号循环0-15
        }
    }

    return frames;
}

void TransportLayer::processSendRequests() {
    while (running_) {
        uint32_t can_id;
        std::vector<uint8_t> data;
        
        if (shm_pool_->readFromSendChannel(can_id, data)) {
            // 记录发送日志
#if _DBUG_
            std::cout << "Sending CAN ID: 0x" << std::hex << can_id << std::dec
                      << ", Length: " << data.size() << std::endl;
#endif
            
            // 实际发送
            sendMessage(can_id, data);
        }
        
        // 清理过期重组上下文
        cleanupExpiredContexts();
        
        // 自适应休眠
        static int idle_count = 0;
        if (data.empty()) {
            std::this_thread::sleep_for(
                std::chrono::microseconds(100 * (1 << std::min(idle_count, 5)))
            );
            idle_count++;
        } else {
            idle_count = 0;
        }
    }
}
