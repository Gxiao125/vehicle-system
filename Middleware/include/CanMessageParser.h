#pragma once
#include <cstdint>
#include <vector>
#include <unordered_map>
#include <functional>
#include <string>
#include <memory>
#include <cmath>

class CanMessageParser{
    public:
        struct SignalDefinition {
            uint8_t start_bit;
            uint8_t length;
            double scale = 1.0;
            double offset = 0.0;
            bool is_signed = false;
            std::string unit;
            double min = 0.0;
            double max = 0.0;
        };
        
        struct MessageDefinition {
            uint32_t can_id;
            uint8_t dlc;
            std::string name;
            std::unordered_map<std::string, SignalDefinition> signals;
        };

        struct SignalValue {
            enum class Type { INT, DOUBLE } type;
            union {
                int64_t int_val;
                double double_val;
            };
            
            // 添加默认构造函数
            SignalValue() : type(Type::DOUBLE), double_val(0.0) {}
            
            SignalValue(int64_t v) : type(Type::INT), int_val(v) {}
            SignalValue(double v) : type(Type::DOUBLE), double_val(v) {}
            
            // 添加赋值运算符
            SignalValue& operator=(int64_t v) {
                type = Type::INT;
                int_val = v;
                return *this;
            }
            
            SignalValue& operator=(double v) {
                type = Type::DOUBLE;
                double_val = v;
                return *this;
            }
            
            // 添加获取值的方法
            double asDouble() const {
                if (type == Type::INT) {
                    return static_cast<double>(int_val);
                }
                return double_val;
            }
            
            int64_t asInt() const {
                if (type == Type::DOUBLE) {
                    return static_cast<int64_t>(double_val);
                }
                return int_val;
            }
        };

        void registerMessage(const MessageDefinition& definition);
        void registerMessage(uint32_t can_id, const MessageDefinition& definition);

        std::unordered_map<std::string, SignalValue>
            parse(uint32_t can_id, const std::vector<uint8_t>& data) const;
        
        std::vector<uint8_t> encode(uint32_t can_id, 
                                    const std::unordered_map<std::string,
                                    SignalValue>& signals) const;

        const MessageDefinition* getDefinition(uint32_t can_id) const;
        const MessageDefinition* getDefinition(const std::string& name) const;

        const std::unordered_map<uint32_t, MessageDefinition>& getMessageDefinitions() const;

        

    
    private:
        std::unordered_map<uint32_t, MessageDefinition> message_definitions_;
        std::unordered_map<std::string, MessageDefinition> message_name_map_;

        uint64_t extractBits(const std::vector<uint8_t>& data,
                                uint8_t start_bit,
                                uint8_t length) const;

        void setBits(std::vector<uint8_t>& data,
                        uint8_t start_bit,
                        uint8_t length,
                        uint64_t value) const;
        
        uint64_t convertToRaw(double physical_value, const SignalDefinition& signal) const;
};
