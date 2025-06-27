#pragma once
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <atomic>
#include <vector>
#include <string>
#include <stdexcept>
#include <cstring>
#include <chrono>
#include <iostream>

// 传输消息结构体
struct TransportMessage {
    uint32_t can_id;        // CAN 消息 ID
    uint64_t timestamp;     // 时间戳（微秒）
    uint16_t data_length;   // 数据长度
    uint8_t data[64];       // 数据缓冲区（最大64字节）
};

// 双缓冲通道结构
struct DoubleBufferChannel {
    std::atomic<size_t> read_index;   // 读索引
    std::atomic<size_t> write_index;  // 写索引
    size_t pool_size;                 // 池大小
    TransportMessage messages[];      // 消息数组（柔性数组）
};

class SharedMemoryPool {
public:
    // 通道类型枚举
    enum ChannelType {
        RECEIVE_CHANNEL,  // 接收通道
        SEND_CHANNEL      // 发送通道
    };

    /**
     * 构造函数
     * @param base_name 共享内存基础名称
     * @param pool_size 每个通道的池大小
     */
    SharedMemoryPool(const std::string& base_name, size_t pool_size = 100);
    
    ~SharedMemoryPool();
    
    // 禁止拷贝
    SharedMemoryPool(const SharedMemoryPool&) = delete;
    SharedMemoryPool& operator=(const SharedMemoryPool&) = delete;
    
    /**
     * 写入消息到接收通道
     * @param can_id CAN 消息 ID
     * @param data 消息数据
     * @return 是否写入成功
     */
    bool writeToReceiveChannel(uint32_t can_id, const std::vector<uint8_t>& data);
    
    /**
     * 从接收通道读取消息
     * @param can_id [out] 读取的 CAN 消息 ID
     * @param data [out] 读取的消息数据
     * @return 是否读取到消息
     */
    bool readFromReceiveChannel(uint32_t& can_id, std::vector<uint8_t>& data);
    
    /**
     * 写入消息到发送通道
     * @param can_id CAN 消息 ID
     * @param data 消息数据
     * @return 是否写入成功
     */
    bool writeToSendChannel(uint32_t can_id, const std::vector<uint8_t>& data);
    
    /**
     * 从发送通道读取消息
     * @param can_id [out] 读取的 CAN 消息 ID
     * @param data [out] 读取的消息数据
     * @return 是否读取到消息
     */
    bool readFromSendChannel(uint32_t& can_id, std::vector<uint8_t>& data);

private:
    // 内部通道指针
    struct ChannelPointers {
        DoubleBufferChannel* recv_channel;  // 接收通道
        DoubleBufferChannel* send_channel;  // 发送通道
    };
    
    /**
     * 创建或打开共享内存通道
     * @param name 共享内存名称
     * @param is_owner 是否是创建者
     * @return 通道指针
     */
    DoubleBufferChannel* createChannel(const std::string& name, bool& is_owner);
    
    /**
     * 写入消息到指定通道
     * @param channel 目标通道
     * @param can_id CAN 消息 ID
     * @param data 消息数据
     * @return 是否写入成功
     */
    bool writeToChannel(DoubleBufferChannel* channel, uint32_t can_id, 
                        const std::vector<uint8_t>& data);
    
    /**
     * 从指定通道读取消息
     * @param channel 源通道
     * @param can_id [out] 读取的 CAN 消息 ID
     * @param data [out] 读取的消息数据
     * @return 是否读取到消息
     */
    bool readFromChannel(DoubleBufferChannel* channel, uint32_t& can_id, 
                         std::vector<uint8_t>& data);

    std::string base_name_;      // 基础名称
    size_t pool_size_;           // 每个通道的池大小
    ChannelPointers channels_;   // 通道指针
    
    // 所有权标志
    struct {
        bool recv_owner;  // 接收通道所有者
        bool send_owner;   // 发送通道所有者
    } ownership_;
};
