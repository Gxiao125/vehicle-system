#pragma once
#include "CanController.h"
#include "SharedMemoryPool.h"
#include <atomic>
#include <thread>
#include <mutex>
#include <functional>
#include <unordered_map>
#include <vector>
#include <chrono>

class TransportLayer {
public:
    // 帧类型枚举
    enum FrameType : uint8_t {
        SINGLE_FRAME = 0x0,
        FIRST_FRAME = 0x1,
        CONSECUTIVE_FRAME = 0x2,
        FLOW_CONTROL_FRAME = 0x3
    };
    
    // 重组上下文结构
    struct ReassemblyContext {
        uint32_t expected_can_id;     // 预期的CAN ID
        size_t total_length;          // 预期的总数据长度
        uint8_t next_sequence;        // 下一个期望的序列号
        uint64_t last_received;       // 最后接收时间（纳秒）
        std::vector<uint8_t> buffer;  // 数据缓冲区
    };
    
    /**
     * @brief 构造函数
     * @param controller CAN控制器引用
     * @param shm_name 共享内存名称
     * @param shm_pool_size 共享内存池大小
     */
    TransportLayer(FlexCANController& controller, 
                  const std::string& shm_name,
                  size_t shm_pool_size);
    
    /**
     * @brief 析构函数
     */
    ~TransportLayer();
    
    // 禁止拷贝和赋值
    TransportLayer(const TransportLayer&) = delete;
    TransportLayer& operator=(const TransportLayer&) = delete;
    
    /**
     * @brief 启动传输层
     */
    void start();
    
    /**
     * @brief 停止传输层
     */
    void stop();
    
    /**
     * @brief 注册消息处理器
     * @param handler 消息处理回调函数
     */
    void registerMessageHandler(
        std::function<void(uint32_t, const std::vector<uint8_t>&)> handler);
    
    /**
     * @brief 发送消息
     * @param can_id CAN消息ID
     * @param data 消息数据
     * @return 成功返回true，失败返回false
     */
    bool sendMessage(uint32_t can_id, const std::vector<uint8_t>& data);

    std::string getShmName() {return getShmName(); }

private:
    /**
     * @brief 处理接收到的CAN帧
     * @param frame 接收到的CAN帧
     */
    void handleReceivedFrame(const EnhancedCANFrame& frame);
    
    /**
     * @brief 处理帧数据
     * @param can_id CAN消息ID
     * @param frame_data 帧数据
     */
    void processFrame(uint32_t can_id, const std::vector<uint8_t>& frame_data);
    
    /**
     * @brief 处理单帧消息
     * @param can_id CAN消息ID
     * @param payload 有效载荷
     */
    void handleSingleFrame(uint32_t can_id, const std::vector<uint8_t>& payload);
    
    /**
     * @brief 处理首帧消息
     * @param can_id CAN消息ID
     * @param payload 有效载荷
     */
    void handleFirstFrame(uint32_t can_id, const std::vector<uint8_t>& payload);
    
    /**
     * @brief 处理连续帧消息
     * @param can_id CAN消息ID
     * @param payload 有效载荷
     */
    void handleConsecutiveFrame(uint32_t can_id, const std::vector<uint8_t>& payload);
    
    /**
     * @brief 清理过期的重组上下文
     */
    void cleanupExpiredContexts();
    
    /**
     * @brief 分割消息为多个CAN帧
     * @param data 原始消息数据
     * @return 分割后的帧列表
     */
    std::vector<std::vector<uint8_t>> segmentMessage(const std::vector<uint8_t>& data);
    
    /**
     * @brief 处理发送请求（从共享内存读取并发送）
     */
    void processSendRequests();

    FlexCANController& can_controller_;  // CAN控制器引用
    std::unique_ptr<SharedMemoryPool> shm_pool_;  // 共享内存池（双缓冲通道）
    
    // 线程控制
    std::atomic<bool> running_{false};
    std::thread receive_processor_thread_;
    std::thread send_processor_thread_;
    
    // 回调处理
    std::function<void(uint32_t, const std::vector<uint8_t>&)> message_handler_;
    std::mutex handler_mutex_;
    
    // 重组上下文
    std::unordered_map<uint32_t, ReassemblyContext> reassembly_contexts_;
    std::mutex reassembly_mutex_;
};
