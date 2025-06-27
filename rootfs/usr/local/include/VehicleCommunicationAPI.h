#pragma once
#include "CanMessageParser.h"
#include "SharedMemoryPool.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <functional>
#include <mutex>
#include <thread>
#include <atomic>
#include <cstdint>

// 前置声明双缓冲通道结构

class VehicleCommunicationAPI {
public:
    // 信号值类型别名
    using SignalValue = CanMessageParser::SignalValue;
    
    // 回调函数类型
    using SignalCallback = std::function<void(const std::string&, double, const std::string&)>;
    using ErrorCallback = std::function<void(uint32_t, const std::string&)>;
    
    /**
     * 构造函数
     * @param shm_pool 共享内存池实例
     */
    explicit VehicleCommunicationAPI(const std::string& shm_name);
    
    ~VehicleCommunicationAPI();
    
    // 禁止拷贝
    VehicleCommunicationAPI(const VehicleCommunicationAPI&) = delete;
    VehicleCommunicationAPI& operator=(const VehicleCommunicationAPI&) = delete;
    
    /**
     * 启动API
     */
    void start();
    
    /**
     * 停止API
     */
    void stop();
    
    /**
     * 加载DBC文件定义
     * @param dbc_file_path DBC文件路径
     */
    void loadMessageDefinitions(const std::string& dbc_file_path);
    
    /**
     * 添加自定义消息定义
     * @param definition 消息定义
     */
    void addMessageDefinition(const CanMessageParser::MessageDefinition& definition);
    
    /**
     * 注册信号处理回调
     * @param signal_name 信号名称
     * @param callback 回调函数
     */
    void registerSignalHandler(const std::string& signal_name, SignalCallback callback);
    
    /**
     * 注册错误处理回调
     * @param callback 错误回调函数
     */
    void registerErrorHandler(ErrorCallback callback);
    
    /**
     * 通过信号名称发送信号
     * @param signal_name 信号名称
     * @param value 信号值
     * @return 是否发送成功
     */
    bool sendSignal(const std::string& signal_name, SignalValue value);
    
    /**
     * 通过CAN ID发送信号
     * @param can_id CAN消息ID
     * @param signal_name 信号名称
     * @param value 信号值
     * @return 是否发送成功
     */
    bool sendSignal(uint32_t can_id, const std::string& signal_name, SignalValue value);
    
    /**
     * 发送原始CAN消息
     * @param can_id CAN消息ID
     * @param data 原始数据
     * @return 是否发送成功
     */
    bool sendRawMessage(uint32_t can_id, const std::vector<uint8_t>& data);

private:
    /**
     * 共享内存读取线程函数
     */
    void shmReaderThreadFunc();
    
    /**
     * 处理接收到的消息
     * @param can_id CAN消息ID
     * @param data 原始数据
     */
    void handleReceivedMessage(uint32_t can_id, const std::vector<uint8_t>& data);
    
    /**
     * 处理错误
     * @param can_id 相关的CAN ID（0表示通用错误）
     * @param message 错误消息
     */
    void handleError(uint32_t can_id, const std::string& message);
    
    SharedMemoryPool* shm_pool_;  // 共享内存池
    std::string shm_name_;
    
    // 回调函数
    std::unordered_map<std::string, SignalCallback> signal_handlers_;
    ErrorCallback error_handler_;
    std::mutex callback_mutex_;  // 保护回调函数
    
    // 消息解析器
    CanMessageParser message_parser_;
    
    // 线程控制
    std::thread shm_reader_thread_;
    std::atomic<bool> running_;
};
