#include "../include/TransportLayer.h"
#include <iostream>
#include <chrono>
#include <iomanip>

TransportLayer::TransportLayer(FlexCANController& controller, 
                             const std::string& shm_name,
                             size_t shm_pool_size)
    : can_controller_(controller),
      shm_pool_(new SharedMemoryPool(shm_name, shm_pool_size)) // 替换 make_unique 为直接 new
{
    // 保存 this 指针用于 lambda（C++11 需要显式处理）
    TransportLayer* self = this;
    
    // 注册帧处理回调（C++11 兼容的 lambda 捕获）
    can_controller_.registerFrameHandler(
        [self](const EnhancedCANFrame& frame) {  // 显式捕获 self 指针
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
    if (shm_pool_->isOwner()) {
        send_processor_thread_ = std::thread(&TransportLayer::processSendRequests, this);
    }
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
    // 零拷贝访问帧数据
    const uint8_t* frame_data = frame.data;
    size_t data_len = frame.can_dlc;
    
    // 创建数据视图避免拷贝
    std::vector<uint8_t> data_view(frame_data, frame_data + data_len);
    processFrame(frame.can_id, data_view);
}

void TransportLayer::processFrame(uint32_t can_id, const std::vector<uint8_t>& frame_data) {
    if (frame_data.empty()) return;

    const uint8_t pci = frame_data[0] >> 4;
    const FrameType type = static_cast<FrameType>(pci);
    
    std::vector<uint8_t> payload(frame_data.begin() + 1, frame_data.end());

    switch (type) {
        case SINGLE_FRAME:
            handleSingleFrame(can_id, payload);
            break;
        case FIRST_FRAME:
            handleFirstFrame(can_id, payload);
            break;
        case CONSECUTIVE_FRAME:
            handleConsecutiveFrame(can_id, payload);
            break;
        case FLOW_CONTROL_FRAME:
            // 流向控制: 简化处理
            break;
        default:
            break;
    }
}

void TransportLayer::handleSingleFrame(uint32_t can_id, const std::vector<uint8_t>& payload) {
    if (payload.empty()) return;

    const size_t length = payload[0] & 0x0F;
    if (length > payload.size() - 1) return;

    std::vector<uint8_t> data(payload.begin() + 1, payload.begin() + 1 + length);
    
    // 调用消息处理器
    {
        std::lock_guard<std::mutex> lock(handler_mutex_);
        if (message_handler_) {
            message_handler_(can_id, data);
        }
    }
    
    // 写入共享内存（使用双缓冲通道）
    shm_pool_->writeMessage(can_id, data);
}

void TransportLayer::handleFirstFrame(uint32_t can_id, const std::vector<uint8_t>& payload) {
    if (payload.size() < 2) return;

    const uint16_t total_length = ((payload[0] & 0x0F) << 8) | payload[1];
    
    std::lock_guard<std::mutex> lock(reassembly_mutex_);
    
    ReassemblyContext ctx;
    ctx.expected_can_id = can_id;
    ctx.total_length = total_length;
    ctx.next_sequence = 1;
    ctx.last_received = std::chrono::steady_clock::now().time_since_epoch().count();
    
    if (payload.size() > 2) {
        ctx.buffer.assign(payload.begin() + 2, payload.end());
    }
    
    reassembly_contexts_[can_id] = ctx;
}

void TransportLayer::handleConsecutiveFrame(uint32_t can_id, const std::vector<uint8_t>& payload) {
    std::lock_guard<std::mutex> lock(reassembly_mutex_);
    
    auto it = reassembly_contexts_.find(can_id);
    if (it == reassembly_contexts_.end()) return;
    
    ReassemblyContext& ctx = it->second;
    if (payload.empty()) {
        reassembly_contexts_.erase(it);
        return;
    }

    const uint8_t seq_num = payload[0] & 0x0F;
    
    if (seq_num != ctx.next_sequence) {
        reassembly_contexts_.erase(it);
        return;
    }
    
    ctx.buffer.insert(ctx.buffer.end(), payload.begin() + 1, payload.end());
    ctx.next_sequence = (ctx.next_sequence + 1) % 16;
    ctx.last_received = std::chrono::steady_clock::now().time_since_epoch().count();
    
    if (ctx.buffer.size() >= ctx.total_length) {
        std::vector<uint8_t> complete_data(ctx.buffer.begin(), 
                                          ctx.buffer.begin() + ctx.total_length);
        
        // 调用消息处理器
        {
            std::lock_guard<std::mutex> hlock(handler_mutex_);
            if (message_handler_) {
                message_handler_(ctx.expected_can_id, complete_data);
            }
        }
        
        // 写入共享内存（使用双缓冲通道）
        shm_pool_->writeMessage(ctx.expected_can_id, complete_data);
        
        reassembly_contexts_.erase(it);
    }
}

void TransportLayer::cleanupExpiredContexts() {
    std::lock_guard<std::mutex> lock(reassembly_mutex_);
    auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    const uint64_t timeout_ns = 1000000000; // 1秒超时
    
    for (auto it = reassembly_contexts_.begin(); it != reassembly_contexts_.end(); ) {
        if (now - it->second.last_received > timeout_ns) {
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
    } else {
        // 首帧
        std::vector<uint8_t> first_frame;
        first_frame.push_back(static_cast<uint8_t>(FIRST_FRAME << 4) | static_cast<uint8_t>((data.size() >> 8) & 0x0F));
        first_frame.push_back(static_cast<uint8_t>(data.size() & 0xFF));
        first_frame.insert(first_frame.end(), data.begin(), data.begin() + std::min<size_t>(6, data.size()));
        frames.push_back(first_frame);

        // 连续帧
        size_t offset = 6;
        uint8_t seq_num = 1;
        while (offset < data.size()) {
            std::vector<uint8_t> frame;
            frame.push_back(static_cast<uint8_t>(CONSECUTIVE_FRAME << 4) | (seq_num & 0x0F));
            size_t chunk_size = std::min<size_t>(7, data.size() - offset);
            frame.insert(frame.end(), data.begin() + offset, data.begin() + offset + chunk_size);
            frames.push_back(frame);

            offset += chunk_size;
            seq_num = (seq_num + 1) % 16;
        }
    }

    return frames;
}

void TransportLayer::processSendRequests() {
    while (running_) {
        uint32_t can_id;
        std::vector<uint8_t> data;
        
        if (shm_pool_->tryReadMessage(can_id, data)) {
            // 记录发送日志
            std::cout << "Sending CAN ID: 0x" << std::hex << can_id << std::dec
                      << ", Length: " << data.size() << std::endl;
            
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
