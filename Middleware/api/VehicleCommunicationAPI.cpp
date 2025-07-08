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
#include <iomanip>
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
    
    std::ostringstream hex_data;
    hex_data << "0x" << std::hex << std::setw(8) << std::setfill('0') << can_id << ": ";
    for (const auto& byte : data) {
        hex_data << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(byte);
    }
    std::cout << hex_data.str() << std::endl;

    // 使用共享内存池接口写入发送通道
    if (!shm_pool_->writeToSendChannel(can_id, data)) {
        handleError(can_id, "Failed to write to send channel");
        return false;
    }
    
    return true;
}

bool VehicleCommunicationAPI::sendMultipleSignals(uint32_t can_id, const std::unordered_map<std::string, SignalValue>& signals) {
    try {
        // 使用消息解析器编码所有信号
        auto data = message_parser_.encode(can_id, signals);
        
        // 发送完整的CAN消息
        return sendRawMessage(can_id, data);
    } catch (const std::exception& e) {
        handleError(can_id, "Failed to send multiple signals: " + std::string(e.what()));
        return false;
    }
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
    std::cout << "=== 开始加载 DBC 文件: " << dbc_file_path << " ===" << std::endl;
    char cwd[1024];
    if (getcwd(cwd, sizeof(cwd))) {
        std::cout << "Current working directory: " << cwd << std::endl;
    }
    
    std::ifstream file(dbc_file_path);
    if (!file.is_open()) {
        handleError(0, "Failed to open DBC file: " + dbc_file_path);
        return;
    }

    auto left_trim = [](std::string& s) {
        s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](int ch) {
            return !std::isspace(ch);
        }));
    };

    auto right_trim = [](std::string& s) {
        s.erase(std::find_if(s.rbegin(), s.rend(), [](int ch) {
            return !std::isspace(ch);
        }).base(), s.end());
    };

    std::string line;
    CanMessageParser::MessageDefinition current_msg;
    bool in_message = false;

    while (std::getline(file, line)) {
        // 去除前后空白
        left_trim(line);
        right_trim(line);

        if (line.empty()) continue;

        if (line.find("BO_") == 0) {
        if (in_message) {
            std::cout << ">> 注册消息: CAN_ID=0x" << std::hex << current_msg.can_id 
                    << " (十进制:" << std::dec << current_msg.can_id << ")"
                    << ", 名称='" << current_msg.name << "', 信号数=" 
                    << current_msg.signals.size() << std::endl;
            message_parser_.registerMessage(current_msg);
        }

        std::istringstream iss(line);
        std::string token;
        iss >> token; // "BO_"
        
        // 1. 读取 CAN ID（十进制）
        uint32_t can_id_decimal;
        if (!(iss >> can_id_decimal)) {
            std::cerr << "!! 无法读取 CAN ID: " << line << std::endl;
            in_message = false;
            continue;
        }
        current_msg.can_id = can_id_decimal;
        
        // 2. 读取消息名称（可能包含空格）
        std::ostringstream name_oss;
        while (iss >> token && token != ":") {
            
            if (!name_oss.str().empty()) name_oss << " ";
            name_oss << token;
            }
            current_msg.name = name_oss.str();
            
            // 3. 读取 DLC
            unsigned int dlc_value;
            if (!(iss >> dlc_value)) {
                std::cerr << "!! 无法读取 DLC: " << line << std::endl;
                in_message = false;
                continue;
            }
            current_msg.dlc = static_cast<uint8_t>(dlc_value);
            
            // 4. 忽略可选的发送节点
            std::string transmitter;
            iss >> transmitter;
            
            // 打印新消息信息
            std::cout << "\n=== 开始解析消息 ===\n"
                    << "CAN ID (十进制): " << current_msg.can_id << "\n"
                    << "CAN ID (十六进制): 0x" << std::hex << current_msg.can_id << std::dec << "\n"
                    << "名称: " << current_msg.name << "\n"
                    << "DLC: " << static_cast<int>(current_msg.dlc) << "\n";

            current_msg.signals.clear();
            in_message = true;

        }
        else if (in_message && line.find("SG_") == 0) {
            std::istringstream iss(line);
            std::string token;
            iss >> token; // "SG_"

            std::string sig_name;
            if (!(iss >> sig_name)) {
                std::cerr << "!! 无法读取信号名称: " << line << std::endl;
                continue;
            }

            CanMessageParser::SignalDefinition sig_def;
            
            // 1. 解析位域部分 (格式: start_bit|length@)
            std::string bit_field;
            if (!(iss >> bit_field)) {
                std::cerr << "!! 无法读取位域: " << line << std::endl;
                continue;
            }

            // 解析位域格式
            size_t pipe_pos = bit_field.find('|');
            size_t at_pos = bit_field.find('@');
            
            if (pipe_pos == std::string::npos || at_pos == std::string::npos) {
                std::cerr << "!! 无效位域格式: " << bit_field << " in " << line << std::endl;
                continue;
            }
            
            try {
                sig_def.start_bit = static_cast<uint16_t>(
                    std::stoul(bit_field.substr(0, pipe_pos)));
                
                sig_def.length = static_cast<uint8_t>(
                    std::stoul(bit_field.substr(pipe_pos + 1, at_pos - pipe_pos - 1)));
            } catch (const std::exception& e) {
                std::cerr << "!! 位域转换错误: " << e.what() 
                          << " in " << bit_field << std::endl;
                continue;
            }

            // 2. 解析字节顺序和符号 (格式: byte_order sign)
            char byte_order_char, sign_char;
            if (!(iss >> byte_order_char >> sign_char)) {
                std::cerr << "!! 无法读取字节顺序/符号: " << line << std::endl;
                continue;
            }
            
            sig_def.byte_order = (byte_order_char == '1') ? 
                CanMessageParser::ByteOrder::_BIG_ENDIAN : 
                CanMessageParser::ByteOrder::_LITTLE_ENDIAN;
            
            sig_def.is_signed = (sign_char == '1');

            // 3. 解析缩放和偏移 (格式: (scale,offset))
            char paren1, comma, paren2;
            double scale, offset;
            if (!(iss >> paren1 >> scale >> comma >> offset >> paren2)) {
                std::cerr << "!! 无法读取缩放/偏移: " << line << std::endl;
                continue;
            }
            if (paren1 != '(' || comma != ',' || paren2 != ')') {
                std::cerr << "!! 缩放/偏移格式错误: " << line << std::endl;
                continue;
            }
            sig_def.scale = scale;
            sig_def.offset = offset;

            // 4. 解析最小/最大值 (格式: [min|max])
            char bracket1, pipe_char, bracket2;
            double min_val, max_val;
            if (!(iss >> bracket1 >> min_val >> pipe_char >> max_val >> bracket2)) {
                std::cerr << "!! 无法读取范围: " << line << std::endl;
                continue;
            }
            if (bracket1 != '[' || pipe_char != '|' || bracket2 != ']') {
                std::cerr << "!! 范围格式错误: " << line << std::endl;
                continue;
            }
            sig_def.min = min_val;
            sig_def.max = max_val;

            // 5. 解析单位
            std::string unit;
            if (iss >> unit) {
                if (unit.size() >= 2 && unit.front() == '"' && unit.back() == '"') {
                    unit = unit.substr(1, unit.size() - 2);
                }
            }
            sig_def.unit = unit;
            
            current_msg.signals[sig_name] = sig_def;

            // 打印信号详情
            std::cout << "  >> 信号: " << sig_name << "\n"
                      << "    起始位: " << sig_def.start_bit 
                      << ", 长度: " << static_cast<int>(sig_def.length) << "\n"
                      << "    字节顺序: " 
                      << (sig_def.byte_order == CanMessageParser::ByteOrder::_BIG_ENDIAN ? "大端" : "小端") 
                      << "\n"
                      << "    有符号: " << (sig_def.is_signed ? "是" : "否") << "\n"
                      << "    缩放: " << sig_def.scale 
                      << ", 偏移: " << sig_def.offset << "\n"
                      << "    范围: [" << sig_def.min << "|" << sig_def.max << "]\n"
                      << "    单位: '" << sig_def.unit << "'\n";
        }
        else if (in_message) {
            // 打印无法识别的行（调试用）
            std::cout << "!! 忽略行: " << line << std::endl;
        }
    }

    if (in_message) {
        std::cout << ">> 注册消息: CAN_ID=0x" << std::hex << current_msg.can_id 
                  << ", 名称='" << current_msg.name << "', 信号数=" 
                  << current_msg.signals.size() << std::dec << std::endl;
        message_parser_.registerMessage(current_msg);
    }

    std::cout << "\n=== DBC 文件解析完成 ===" << std::endl;
    std::cout << "总共解析消息数: " << message_parser_.getMessageDefinitions().size() << std::endl;
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
