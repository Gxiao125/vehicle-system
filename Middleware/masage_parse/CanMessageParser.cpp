#include "../include/CanMessageParser.h"
#include <stdexcept>
#include <cmath>
#include <sstream>

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
    return it != message_name_map_.end() ? &it ->second : nullptr;
}


std::unordered_map<std::string, CanMessageParser::SignalValue> 
CanMessageParser::parse(uint32_t can_id, const std::vector<uint8_t>& data) const {
    auto it = message_definitions_.find(can_id);
    if (it == message_definitions_.end()) {
        return std::unordered_map<std::string, SignalValue>();
    }

    const MessageDefinition& def = it->second;
    std::unordered_map<std::string, CanMessageParser::SignalValue> result;

    for (auto signal_it = def.signals.begin(); signal_it != def.signals.end(); ++signal_it) {
        const std::string& signal_name = signal_it->first;
        const SignalDefinition& signal_def = signal_it->second;

        uint64_t raw_value = extractBits(data, signal_def.start_bit, signal_def.length);

        if (signal_def.is_signed) {
            // 正确的符号扩展处理
            if (signal_def.length < 64 && (raw_value & (1ULL << (signal_def.length - 1)))) {
                // 计算符号扩展掩码
                uint64_t mask = (1ULL << signal_def.length) - 1;
                // 扩展符号位
                raw_value |= ~mask;
            }

            int64_t signed_value = static_cast<int64_t>(raw_value);
            double physical_value = static_cast<double>(signed_value) * signal_def.scale + signal_def.offset;

            // 使用 double 类型的 SignalValue
            result[signal_name] = SignalValue(physical_value);
        } else {
            double physical_value = static_cast<double>(raw_value) * signal_def.scale + signal_def.offset;
            result[signal_name] = SignalValue(physical_value);
        }
    }

    return result;
}

uint64_t CanMessageParser::extractBits(const std::vector<uint8_t>& data,
                                        uint8_t start_bit,
                                        uint8_t length) const
{
    uint64_t result = 0;
    uint8_t current_bit = start_bit;
    uint8_t bits_remaining =length;

    while (bits_remaining > 0) {
        uint8_t byte_index = current_bit/8;
        uint8_t bit_index = current_bit%8;
        uint8_t bits_in_current_byte = std::min(bits_remaining, static_cast<uint8_t>(8 - bit_index));

        if (byte_index >= data.size()) {
            break;
        }

        uint8_t mask = (1 << bits_in_current_byte) - 1;
        uint8_t value = (data[byte_index] >> bit_index) & mask;

        result |= (static_cast<uint64_t>(value) << (length - bits_remaining));

        current_bit += bits_in_current_byte;
        bits_remaining -= bits_in_current_byte;
    }


    return result;
}

uint64_t CanMessageParser::convertToRaw(double physical_value, const SignalDefinition& signal) const {
    // 应用反向转换: raw = (physical_value - offset) / scale
    double raw_value = (physical_value - signal.offset) / signal.scale;
    // 处理有符号值
    if (signal.is_signed) {
        int64_t signed_value = static_cast<ino64_t>(std::round(raw_value));
        // 应用位掩码
        return static_cast<uint64_t>(signed_value) & ((1ULL << signal.length) - 1); 
    } else {
        uint64_t unsigned_value = static_cast<uint64_t>(std::round(raw_value));

        return unsigned_value & ((1ULL << signal.length) -1);
    }
}

void CanMessageParser::setBits(std::vector<uint8_t>&data,
                                uint8_t start_bit,
                                uint8_t length,
                                uint64_t value) const {
    uint8_t current_bit = start_bit;
    uint8_t bits_remaining = length;
    uint64_t value_to_set = value;
    
    while (bits_remaining > 0) {
        uint8_t byte_index = current_bit / 8;
        uint8_t bit_index = current_bit % 8;
        uint8_t bits_in_current_byte = std::min(bits_remaining, static_cast<uint8_t>(8 - bit_index));

        if (byte_index >= data.size()) {
            data.resize(byte_index + 1, 0);
        }

        uint8_t mask = ((1 << bits_in_current_byte) - 1) << bit_index;
        uint8_t value_type = (value_to_set << (64 - bits_remaining)) >> (64 - bits_in_current_byte);
        data[byte_index] = (data[byte_index] & mask) | (value_type << bit_index);

        current_bit += bits_in_current_byte;
        bits_remaining -= bits_in_current_byte;
    }
}

std::vector<uint8_t> CanMessageParser::encode(uint32_t can_id, 
                            const std::unordered_map<std::string,
                            CanMessageParser::SignalValue>& signals) const{
    auto it = message_definitions_.find(can_id);
    if (it == message_definitions_.end()) {
        throw std::runtime_error("Message definition not found for CAN ID" + std::to_string(can_id));
    }

    const MessageDefinition& def = it->second;
    std::vector<uint8_t> data(def.dlc, 0);// 初始化数据向量，长度为 DLC

    for (auto it_signal = signals.begin(); it_signal != signals.end(); ++it_signal) {
        const std::string& signal_name = it_signal->first;
        const SignalValue& value = it_signal->second;

        auto signal_it = def.signals.find(signal_name);
        if (signal_it == def.signals.end()) {
            throw std::runtime_error("Signal not found:" + signal_name);
        }

        const SignalDefinition& signal_def = signal_it->second;
        double physical_value = 0.0;

        // 根据 SignalValue 类型获取值
        if(value.type == SignalValue::Type::DOUBLE) {
            physical_value = value.double_val;
        } else if (value.type == SignalValue::Type::INT){
            physical_value = static_cast<double>(value.int_val);
        } else {
            throw std::runtime_error("Invalid signal value type for signal" + signal_name);
        }

        // 边界检查（仅当 min 和 max 不同时执行）
        if (signal_def.min != signal_def.max) {
            if (physical_value < signal_def.min || physical_value > signal_def.max) {
                std::ostringstream oss;
                oss << "Signal value out of range:" << signal_name
                    <<"(" << physical_value << "not in ["
                    << signal_def.min << "," << signal_def.max << "])";
                
                throw std::runtime_error(oss.str());
            }
        }
        uint64_t raw_value = convertToRaw(physical_value, signal_def);
        setBits(data, signal_def.start_bit, signal_def.length, raw_value);
    }

    return data;
}
const std::unordered_map<uint32_t, CanMessageParser::MessageDefinition>& CanMessageParser::getMessageDefinitions() const {
            return message_definitions_;
}
