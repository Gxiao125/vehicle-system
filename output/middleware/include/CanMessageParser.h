#include <cstdint>  // 包含 uint8_t, uint16_t, uint32_t 等类型
#include <vector>
#include <string>
#include <unordered_map>
#include <stdexcept>

class CanMessageParser {
public:
    enum ByteOrder {
        _BIG_ENDIAN,
        _LITTLE_ENDIAN
    };

    struct SignalDefinition {
        std::string name;
        uint16_t start_bit;
        uint8_t length;
        double scale;
        double offset;
        double min;
        double max;
        bool is_signed;
        std::string unit;
        ByteOrder byte_order;
    };

    struct MessageDefinition {
        uint32_t can_id;
        std::string name;
        uint8_t dlc;
        std::unordered_map<std::string, SignalDefinition> signals;
    };

    struct SignalValue {
        enum Type { INT, DOUBLE } type;
        union {
            int64_t int_val;
            double double_val;
        };
        
        // 添加默认构造函数
        SignalValue() : type(DOUBLE), double_val(0.0) {}
        
        SignalValue(int64_t v) : type(INT), int_val(v) {}
        SignalValue(double v) : type(DOUBLE), double_val(v) {}
        
        // 添加赋值运算符
        SignalValue& operator=(int64_t v) {
            type = INT;
            int_val = v;
            return *this;
        }
        
        SignalValue& operator=(double v) {
            type = DOUBLE;
            double_val = v;
            return *this;
        }
        
        // 添加获取值的方法
        double asDouble() const {
            if (type == INT) {
                return static_cast<double>(int_val);
            }
            return double_val;
        }
        
        int64_t asInt() const {
            if (type == DOUBLE) {
                return static_cast<int64_t>(double_val);
            }
            return int_val;
        }
    };

    void registerMessage(const MessageDefinition& definition);
    void registerMessage(uint32_t can_id, const MessageDefinition& definition);
    const MessageDefinition* getDefinition(uint32_t can_id) const;
    const MessageDefinition* getDefinition(const std::string& name) const;

    std::unordered_map<std::string, SignalValue> parse(
        uint32_t can_id, const std::vector<uint8_t>& data) const;

    std::vector<uint8_t> encode(
        uint32_t can_id,
        const std::unordered_map<std::string, SignalValue>& signals) const;

    const std::unordered_map<uint32_t, MessageDefinition>& getMessageDefinitions() const;

private:
    uint64_t extractBits(
        const std::vector<uint8_t>& data,
        uint16_t start_bit,
        uint8_t length,
        ByteOrder byte_order) const;

    uint64_t convertToRaw(
        double physical_value,
        const SignalDefinition& signal) const;

    void setBits(
        std::vector<uint8_t>& data,
        uint16_t start_bit,
        uint8_t length,
        uint64_t value,
        ByteOrder byte_order) const;

    std::unordered_map<uint32_t, MessageDefinition> message_definitions_;
    std::unordered_map<std::string, MessageDefinition> message_name_map_;
};
