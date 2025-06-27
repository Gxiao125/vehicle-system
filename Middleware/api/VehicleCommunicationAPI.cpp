#include "../include/VehicleCommunicationAPI.h"
#include "../include/SharedMemoryPool.h" 
#include <fstream>
#include <sstream>
#include <cctype>
#include <algorithm>
#include <iostream>
#include <utility>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <atomic>
#include <thread>
#include <cstring>
// 共享内存消息结构


VehicleCommunicationAPI::VehicleCommunicationAPI(const std::string& shm_name)
    : shm_name_(shm_name), running_(false) {
    // 初始化错误处理
    error_handler_ = [](uint32_t can_id, const std::string& message) {
        std::cerr << "CAN Error [" << std::hex << can_id << std::dec << "]: " << message << std::endl;
    };
    
    try {
    // 创建共享内存池实例
        shm_pool_ = new SharedMemoryPool(shm_name, 64); // 注意：需要与TransportLayer使用相同的大小
    }  catch (const std::exception& e) {
        handleError(0, "Failed to create shared memory pool: " + std::string(e.what()));
    }
}

VehicleCommunicationAPI::~VehicleCommunicationAPI() {
    stop();
}

void VehicleCommunicationAPI::start() {
    if (running_) return;
    
    running_ = true;
    
    // 启动读取线程
    shm_reader_thread_ = std::thread(&VehicleCommunicationAPI::shmReaderThreadFunc, this);
}

void VehicleCommunicationAPI::stop() {
    running_ = false;
    
    if (shm_reader_thread_.joinable()) {
        shm_reader_thread_.join();
    }
}

void VehicleCommunicationAPI::registerSignalHandler(const std::string& signal_name, SignalCallback callback) {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    signal_handlers_[signal_name] = std::move(callback);
}

void VehicleCommunicationAPI::registerErrorHandler(ErrorCallback callback) {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    error_handler_ = std::move(callback);
}

bool VehicleCommunicationAPI::sendSignal(const std::string& signal_name, SignalValue value) {
    const CanMessageParser::MessageDefinition* msg_def = nullptr;
    uint32_t can_id = 0;

    const auto& msg_defs = message_parser_.getMessageDefinitions();

    for (const auto& item : msg_defs) {
        uint32_t id = item.first;
        const auto& def = item.second;
        
        if (def.signals.find(signal_name) != def.signals.end()) {
            msg_def = &def;
            can_id = id;
            break;
        }
    }

    if (!msg_def) {
        handleError(0, "Signal definition not found: " + signal_name);
        return false;
    }

    return sendSignal(can_id, signal_name, value);
}

bool VehicleCommunicationAPI::sendSignal(uint32_t can_id, const std::string& signal_name, SignalValue value) {
    try {
        std::unordered_map<std::string, SignalValue> signals;
        signals[signal_name] = value;

        auto data = message_parser_.encode(can_id, signals);
        return sendRawMessage(can_id, data);
    } catch (const std::exception& e) {
        handleError(can_id, "Failed to send signal: " + std::string(e.what()));
        return false;
    }
}

bool VehicleCommunicationAPI::sendRawMessage(uint32_t can_id, const std::vector<uint8_t>& data) {
    if (!shm_pool_) {
        handleError(can_id, "Shared memory not initialized");
        return false;
    }
    
    // 使用共享内存池接口写入发送通道
    if (!shm_pool_->writeToSendChannel(can_id, data)) {
        handleError(can_id, "Failed to write to send channel");
        return false;
    }
    
    return true;
}

void VehicleCommunicationAPI::shmReaderThreadFunc() {
    while (running_) {
        uint32_t can_id;
        std::vector<uint8_t> data;
        
        // 使用共享内存池接口从接收通道读取消息
        if (shm_pool_->readFromReceiveChannel(can_id, data)) {
            // 处理消息
            handleReceivedMessage(can_id, data);
        } else {
            // 没有数据时休眠
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
}

void VehicleCommunicationAPI::loadMessageDefinitions(const std::string& dbc_file_path) {

    char cwd[1024];
    if (getcwd(cwd, sizeof(cwd))) {
        std::cout << "Current working directory: " << cwd << std::endl;
    }
    
    std::ifstream file(dbc_file_path);
    if (!file.is_open()) {
        handleError(0, "Failed to open DBC file: " + dbc_file_path);
        return;
    }

    std::string line;
    CanMessageParser::MessageDefinition current_msg;
    bool in_message = false;

    while (std::getline(file, line)) {
        // 去除前后空白
        auto left_trim = [](std::string& s) {
            s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](int ch){
                return !std::isspace(ch);
            }));
        };

        auto right_trim = [](std::string& s) {
            s.erase(std::find_if(s.rbegin(), s.rend(), [](int ch) {
                return !std::isspace(ch);
            }).base(), s.end());
        };
        
        left_trim(line);
        right_trim(line);

        if (line.empty()) continue;

        if (line.find("BO_") == 0) {
            if (in_message) {
                message_parser_.registerMessage(current_msg);
            }

            std::istringstream iss(line);
            std::string token;
            iss >> token; // "BO_"
            iss >> current_msg.can_id;
            iss >> current_msg.name;
            iss >> token; // ":"
            iss >> current_msg.dlc;

            current_msg.signals.clear();
            in_message = true;
        }
        else if (in_message && line.find("SG_") == 0) {
            std::istringstream iss(line);
            std::string token;
            iss >> token; // "SG_"

            CanMessageParser::SignalDefinition sig_def;
            std::string sig_name;
            iss >> sig_name;
            
            // 解析起始位和长度 (格式: "start_bit|length@")
            char pipe, at_sign;
            iss >> sig_def.start_bit >> pipe >> sig_def.length >> at_sign;

            // 解析字节顺序 (1=大端, 0=小端) 和符号 (0=无符号, 1=有符号)
            char byte_order, sign;
            iss >> byte_order >> sign;
            sig_def.is_signed = (sign == '1');

            // 解析缩放因子和偏移量 (格式: "(scale,offset)")
            char paren, comma;
            iss >> paren >> sig_def.scale >> comma >> sig_def.offset >> paren;

            // 解析最小值和最大值 (格式: "[min|max]")
            char bracket;
            iss >> bracket >> sig_def.min >> pipe >> sig_def.max >> bracket;

            // 解析单位 (格式: "unit")
            std::string unit;
            iss >> unit;
            if (!unit.empty() && unit.front() == '"' && unit.back() == '"') {
                unit = unit.substr(1, unit.size() - 2);
            }
            sig_def.unit = unit;
            
            current_msg.signals[sig_name] = sig_def;
        }
    }

    if (in_message) {
        message_parser_.registerMessage(current_msg);
    }
}

void VehicleCommunicationAPI::addMessageDefinition(const CanMessageParser::MessageDefinition& definition) {
    message_parser_.registerMessage(definition);
}

void VehicleCommunicationAPI::handleReceivedMessage(uint32_t can_id, const std::vector<uint8_t>& data) {
    try {
        auto parsed_signals = message_parser_.parse(can_id, data);

        std::lock_guard<std::mutex> lock(callback_mutex_);
        for (const auto& signal : parsed_signals) {
            const std::string& signal_name = signal.first;
            const auto& value = signal.second;

            auto handler_it = signal_handlers_.find(signal_name);
            if (handler_it != signal_handlers_.end()) {
                const auto *def = message_parser_.getDefinition(can_id);
                std::string unit = "";
                if (def && def->signals.find(signal_name) != def->signals.end()) {
                    unit = def->signals.at(signal_name).unit;
                }

                handler_it->second(signal_name, value.asDouble(), unit);
            }
        }
    } catch (const std::exception& e) {
        handleError(can_id, "Failed to parse message: " + std::string(e.what()));
    }
}

void VehicleCommunicationAPI::handleError(uint32_t can_id, const std::string& message) {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    if (error_handler_) {
        error_handler_(can_id, message);
    } else {
        std::cerr << "CAN Error [" << std::hex << can_id << std::dec << "]: " << message << std::endl;
    }
}
