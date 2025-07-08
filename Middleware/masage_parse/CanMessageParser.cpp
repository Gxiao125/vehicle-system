#include "../include/CanMessageParser.h"
#include <cmath>
#include <sstream>
#include <iomanip>
#include <iostream>
#include <algorithm>
#include <iomanip>  // 添加用于十六进制格式化的头文件

void CanMessageParser::registerMessage(const MessageDefinition& definition) {
    message_definitions_[definition.can_id] = definition;
    message_name_map_[definition.name] = definition;
}

void CanMessageParser::registerMessage(uint32_t can_id, const MessageDefinition& definition) {
    message_definitions_[can_id] = definition;
    message_name_map_[definition.name] = definition;
}

const CanMessageParser::MessageDefinition* CanMessageParser::getDefinition(uint32_t can_id) const {
    auto it = message_definitions_.find(can_id);
    return it != message_definitions_.end() ? &it->second : nullptr;
}

const CanMessageParser::MessageDefinition* CanMessageParser::getDefinition(const std::string& name) const {
    auto it = message_name_map_.find(name);
    return it != message_name_map_.end() ? &it->second : nullptr;
}

std::unordered_map<std::string, CanMessageParser::SignalValue> 
CanMessageParser::parse(uint32_t can_id, const std::vector<uint8_t>& data) const {
    auto it = message_definitions_.find(can_id);
    if (it == message_definitions_.end()) {
        throw std::runtime_error("Message definition not found for CAN ID: " + std::to_string(can_id));
    }

    const MessageDefinition& def = it->second;
    std::unordered_map<std::string, SignalValue> result;

    for (const auto& signal_entry : def.signals) {
        const std::string& signal_name = signal_entry.first;
        const SignalDefinition& signal_def = signal_entry.second;
        
        uint64_t raw_value = extractBits(data, signal_def.start_bit, signal_def.length, signal_def.byte_order);

        // 处理有符号值（符号扩展）
        if (signal_def.is_signed) {
            if (signal_def.length < 64) {
                uint64_t sign_bit = 1ULL << (signal_def.length - 1);
                if (raw_value & sign_bit) {
                    uint64_t mask = (1ULL << signal_def.length) - 1;
                    raw_value |= ~mask; // 设置所有高位为1
                }
            }
        }

        // 转换为物理值
        double physical_value = signal_def.is_signed ? 
            static_cast<double>(static_cast<int64_t>(raw_value)) : 
            static_cast<double>(raw_value);
            
        physical_value = physical_value * signal_def.scale + signal_def.offset;

        // 边界检查
        if (physical_value < signal_def.min) physical_value = signal_def.min;
        if (physical_value > signal_def.max) physical_value = signal_def.max;

        result[signal_name] = SignalValue(physical_value);
    }

    return result;
}

uint64_t CanMessageParser::extractBits(const std::vector<uint8_t>& data,
                                      uint16_t start_bit,
                                      uint8_t length,
                                      ByteOrder byte_order) const {
    if (data.empty()) return 0;
    
    uint64_t result = 0;
    uint16_t current_bit = start_bit;
    uint8_t bits_remaining = length;
    const size_t total_bits = data.size() * 8;

    // 确定字节顺序
    const bool is_big_endian = (byte_order == ByteOrder::_BIG_ENDIAN);

    while (bits_remaining > 0 && current_bit < total_bits) {
        size_t byte_index = current_bit / 8;
        uint8_t bit_index = current_bit % 8;
        
        // 计算当前字节中可以提取的位数
        uint8_t bits_to_extract = std::min(bits_remaining, static_cast<uint8_t>(8 - bit_index));
        
        // 从当前字节提取位
        uint8_t byte_value = data[byte_index];
        uint8_t mask = (1 << bits_to_extract) - 1;
        uint8_t extracted_bits = (byte_value >> bit_index) & mask;
        
        // 根据字节顺序添加到结果中
        if (is_big_endian) {
            result = (result << bits_to_extract) | extracted_bits;
        } else {
            result |= static_cast<uint64_t>(extracted_bits) << (length - bits_remaining);
        }
        
        // 更新位置和剩余位数
        current_bit += bits_to_extract;
        bits_remaining -= bits_to_extract;
    }

    return result;
}

uint64_t CanMessageParser::convertToRaw(double physical_value, const SignalDefinition& signal) const {
    // 应用反向转换: raw = (physical_value - offset) / scale
    double raw_value_double = (physical_value - signal.offset) / signal.scale;
    
    // 调试输出 - 显示计算过程
    std::cout << "  [convertToRaw] signal: " << signal.name << "\n"
              << "    physical_value: " << physical_value << "\n"
              << "    offset: " << signal.offset << "\n"
              << "    scale: " << signal.scale << "\n"
              << "    raw_value_double: " << raw_value_double << std::endl;
    
    // 四舍五入
    int64_t int_value = static_cast<int64_t>(std::round(raw_value_double));
    
    // 处理有符号值
    uint64_t raw_value;
    if (signal.is_signed) {
        // 应用位掩码
        uint64_t mask = (1ULL << signal.length) - 1;
        raw_value = static_cast<uint64_t>(int_value) & mask;
        
        // 调试输出
        std::cout << "    Signed signal | Mask: 0x" << std::hex << mask << std::dec
                  << " | Raw value: " << raw_value << std::endl;
    } else {
        if (int_value < 0) {
            // 调试输出
            std::cout << "    Unsigned signal | Negative value clamped to 0" << std::endl;
            int_value = 0;
        }
        raw_value = static_cast<uint64_t>(int_value);
        
        // 调试输出
        std::cout << "    Unsigned signal | Raw value: " << raw_value << std::endl;
    }
    
    return raw_value;
}
void CanMessageParser::setBits(std::vector<uint8_t>& data,
                              uint16_t start_bit,
                              uint8_t length,
                              uint64_t value,
                              ByteOrder byte_order) const {
    if (data.empty()) return;
    
    uint16_t current_bit = start_bit;
    uint8_t bits_remaining = length;
    const size_t total_bits = data.size() * 8;
    const bool is_big_endian = (byte_order == ByteOrder::_BIG_ENDIAN);

    while (bits_remaining > 0 && current_bit < total_bits) {
        size_t byte_index = current_bit / 8;
        uint8_t bit_index = current_bit % 8;
        
        // 计算当前字节可以设置的位数
        uint8_t bits_to_set = std::min(bits_remaining, static_cast<uint8_t>(8 - bit_index));
        
        // 创建掩码
        uint8_t mask = ((1 << bits_to_set) - 1) << bit_index;
        
        // 获取要设置的值部分
        uint8_t value_part;
        if (is_big_endian) {
            // 大端序：从高位开始
            value_part = static_cast<uint8_t>(
                (value >> (bits_remaining - bits_to_set)) & ((1 << bits_to_set) - 1));
        } else {
            // 小端序：从低位开始
            value_part = static_cast<uint8_t>(value & ((1 << bits_to_set) - 1));
            value >>= bits_to_set;
        }
        
        // 移动值到正确位置
        value_part <<= bit_index;
        
        // 清除目标位然后设置新值
        data[byte_index] = (data[byte_index] & ~mask) | (value_part & mask);
        
        // 更新位置和剩余位数
        current_bit += bits_to_set;
        bits_remaining -= bits_to_set;
    }
}

std::vector<uint8_t> CanMessageParser::encode(uint32_t can_id, 
                            const std::unordered_map<std::string,
                            SignalValue>& signals) const {
    auto it = message_definitions_.find(can_id);
    if (it == message_definitions_.end()) {
        throw std::runtime_error("Message definition not found for CAN ID: " + std::to_string(can_id));
    }

    const MessageDefinition& def = it->second;
    std::vector<uint8_t> data(def.dlc, 0); // 初始化数据向量，长度为 DLC
    
    // 调试：打印消息信息
    std::cout << "Encoding message: CAN_ID=0x" << std::hex << can_id << std::dec
              << ", Name=" << def.name
              << ", DLC=" << def.dlc
              << std::endl;

    for (const auto& signal_entry : signals) {
        const std::string& signal_name = signal_entry.first;
        const SignalValue& value = signal_entry.second;
        
        auto signal_it = def.signals.find(signal_name);
        if (signal_it == def.signals.end()) {
            throw std::runtime_error("Signal not found: " + signal_name);
        }

        const SignalDefinition& signal_def = signal_it->second;
        double physical_value = 0.0;

        // 根据 SignalValue 类型获取值
        if (value.type == SignalValue::Type::DOUBLE) {
            physical_value = value.double_val;
        } else if (value.type == SignalValue::Type::INT) {
            physical_value = static_cast<double>(value.int_val);
        } else {
            throw std::runtime_error("Invalid signal value type for signal: " + signal_name);
        }

        // 边界检查
        if (physical_value < signal_def.min || physical_value > signal_def.max) {
            std::ostringstream oss;
            oss << "Signal value out of range: " << signal_name
                << " (" << physical_value << " not in ["
                << signal_def.min << ", " << signal_def.max << "])";
            throw std::runtime_error(oss.str());
        }

        uint64_t raw_value = convertToRaw(physical_value, signal_def);
        
        // 调试：打印信号转换信息
        std::cout << "  Signal: " << signal_name
                  << " | Physical: " << physical_value
                  << " | Raw: " << raw_value
                  << " | Start bit: " << signal_def.start_bit
                  << " | Length: " << signal_def.length
                  << " | Byte order: " << (signal_def.byte_order == ByteOrder::_BIG_ENDIAN ? "BIG" : "LITTLE")
                  << " | Scale: " << signal_def.scale
                  << " | Offset: " << signal_def.offset
                  << " | Signed: " << (signal_def.is_signed ? "Yes" : "No")
                  << std::endl;

        setBits(data, signal_def.start_bit, signal_def.length, raw_value, signal_def.byte_order);
    }

    // 调试：打印编码后的数据
    std::cout << "  Encoded data: ";
    for (uint8_t byte : data) {
        std::cout << std::hex << std::setw(2) << std::setfill('0') 
                  << static_cast<int>(byte) << " ";
    }
    std::cout << std::dec << std::endl;

    return data;
}


const std::unordered_map<uint32_t, CanMessageParser::MessageDefinition>& 
CanMessageParser::getMessageDefinitions() const {
    return message_definitions_;
}