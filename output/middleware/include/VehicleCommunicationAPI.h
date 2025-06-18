#pragma once
#include "CanMessageParser.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <functional>
#include <mutex>
#include <thread>
#include <atomic>
#include <cstdint>

// 前置声明双缓冲通道结构
struct DoubleBufferChannel;

class VehicleCommunicationAPI {
public:
    using SignalValue = CanMessageParser::SignalValue;
    using SignalCallback = std::function<void(const std::string&, double, const std::string&)>;
    using ErrorCallback = std::function<void(uint32_t, const std::string&)>;

    explicit VehicleCommunicationAPI(const std::string& shm_name);
    ~VehicleCommunicationAPI();

    // 禁止拷贝和赋值
    VehicleCommunicationAPI(const VehicleCommunicationAPI&) = delete;
    VehicleCommunicationAPI& operator=(const VehicleCommunicationAPI&) = delete;

    void start();
    void stop();

    void registerSignalHandler(const std::string& signal_name, SignalCallback callback);
    void registerErrorHandler(ErrorCallback callback);

    bool sendSignal(const std::string& signal_name, SignalValue value);
    bool sendSignal(uint32_t can_id, const std::string& signal_name, SignalValue value);
    bool sendRawMessage(uint32_t can_id, const std::vector<uint8_t>& data);

    void loadMessageDefinitions(const std::string& dbc_file_path);
    void addMessageDefinition(const CanMessageParser::MessageDefinition& definition);

private:
    void shmReaderThreadFunc();
    void handleReceivedMessage(uint32_t can_id, const std::vector<uint8_t>& data);
    void handleError(uint32_t can_id, const std::string& message);

    std::string shm_name_;
    bool running_ = false;
    int shm_fd_ = -1;
    DoubleBufferChannel* channel_ = nullptr;  // 使用双缓冲通道指针
    std::thread shm_reader_thread_;
    CanMessageParser message_parser_;
    
    // 回调处理
    std::unordered_map<std::string, SignalCallback> signal_handlers_;
    ErrorCallback error_handler_;
    std::mutex callback_mutex_;
};

