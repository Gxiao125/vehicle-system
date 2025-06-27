#pragma once
#include "CanController.h"
#include "SharedMemoryPool.h"
#include <atomic>
#include <thread>
#include <mutex>
#include <functional>
#include <unordered_map>
#include <vector>
#include <chrono>



class TransportLayer {
public:
    enum FrameType : uint8_t {
        SINGLE_FRAME = 0x0,
        FIRST_FRAME = 0x1,
        CONSECUTIVE_FRAME = 0x2,
        FLOW_CONTROL_FRAME = 0x3
    };

    TransportLayer(FlexCANController& controller, 
                  const std::string& shm_name,
                  size_t shm_pool_size);
    ~TransportLayer();

    void start();
    void stop();

    void registerMessageHandler(
        std::function<void(uint32_t, const std::vector<uint8_t>&)> handler);

    bool sendMessage(uint32_t can_id, const std::vector<uint8_t>& data);

private:
    struct ReassemblyContext {
        uint32_t expected_can_id;
        uint16_t total_length;
        uint8_t next_sequence;
        uint64_t last_received;
        std::vector<uint8_t> buffer;
    };

    void handleReceivedFrame(const EnhancedCANFrame& frame);
    void processFrame(uint32_t can_id, const uint8_t* frame_data, size_t data_len);
    void handleSingleFrame(uint32_t can_id, const uint8_t* frame_data, size_t data_len);
    void handleFirstFrame(uint32_t can_id, const uint8_t* frame_data, size_t data_len);
    void handleConsecutiveFrame(uint32_t can_id, const uint8_t* frame_data, size_t data_len);
    void deliverMessage(uint32_t can_id, const std::vector<uint8_t>& data);
    void cleanupExpiredContexts();
    std::vector<std::vector<uint8_t>> segmentMessage(const std::vector<uint8_t>& data);
    void processSendRequests();

    FlexCANController& can_controller_;
    std::unique_ptr<SharedMemoryPool> shm_pool_;

    std::function<void(uint32_t, const std::vector<uint8_t>&)> message_handler_;
    std::mutex handler_mutex_;

    std::unordered_map<uint32_t, ReassemblyContext> reassembly_contexts_;
    std::mutex reassembly_mutex_;

    std::thread receive_processor_thread_;
    std::thread send_processor_thread_;
    std::atomic<bool> running_{false};
};
