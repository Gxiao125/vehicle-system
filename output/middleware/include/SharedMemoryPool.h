#pragma once
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <string>
#include <stdexcept>
#include <vector>
#include <atomic>

struct TransportMessage {
    uint32_t can_id;            // CAN消息ID
    uint64_t timestamp;         // 时间戳（微秒）
    uint16_t data_length;       // 数据长度
    uint8_t data[4096];         // 数据缓冲区（ISO-TP最大长度）
};

// 双缓冲通道结构体
struct DoubleBufferChannel {
    std::atomic<size_t> read_index;   // 读索引（原子操作）
    std::atomic<size_t> write_index;  // 写索引（原子操作）
    size_t pool_size;                 // 缓冲池大小
    TransportMessage messages[0];     // 柔性数组（消息缓冲区）
};

class SharedMemoryPool {
public:
    /**
     * @brief 构造函数，创建或附加到共享内存池
     * @param name 共享内存名称
     * @param pool_size 缓冲池大小
     * @throws std::runtime_error 如果共享内存操作失败
     */
    SharedMemoryPool(const std::string& name, size_t pool_size);
    
    /**
     * @brief 析构函数，清理共享内存资源
     */
    ~SharedMemoryPool();
    
    // 禁止拷贝和赋值
    SharedMemoryPool(const SharedMemoryPool&) = delete;
    SharedMemoryPool& operator=(const SharedMemoryPool&) = delete;
    
    /**
     * @brief 写入消息到共享内存
     * @param can_id CAN消息ID
     * @param data 消息数据
     * @return 成功返回true，失败返回false
     */
    bool writeMessage(uint32_t can_id, const std::vector<uint8_t>& data);
    
    /**
     * @brief 尝试从共享内存读取消息
     * @param can_id [out] 读取到的CAN消息ID
     * @param data [out] 读取到的消息数据
     * @return 成功读取返回true，无数据返回false
     */
    bool tryReadMessage(uint32_t& can_id, std::vector<uint8_t>& data);
    
    /**
     * @brief 检查当前进程是否是共享内存的所有者
     * @return 如果是创建者返回true，如果是附加者返回false
     */
    bool isOwner() const { return is_owner_; }

    std::string getShmName() { return  shm_name_; }

private:
    std::string shm_name_;          // 共享内存名称
    size_t pool_size_;              // 缓冲池大小
    int shm_fd_ = -1;               // 共享内存文件描述符
    bool is_owner_ = false;         // 是否是共享内存的所有者
    DoubleBufferChannel* channel_ = nullptr; // 双缓冲通道指针
};

