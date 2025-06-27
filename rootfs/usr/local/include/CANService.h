#pragma once
#include "CanController.h"
#include "TransportLayer.h"
#include <functional>
#include <string>
#include <vector>
#include <atomic>
#include <csignal>

class CANService {
public:
    CANService(const std::string& device_path,
               const std::string& shm_name = "/can_shared_mem",
               size_t shm_pool_size = 64);
    
    void start();
    void stop();
    bool isRunning() const { return running_; }

    void registerMessageHandler(std::function<void(uint32_t, const std::vector<uint8_t>&)> handler);

private:
    FlexCANController can_controller_;
    TransportLayer transport_layer_;
    std::atomic<bool> running_{false};
    
    static void signalHandler(int signal);
    static std::atomic<bool> global_running_;
};

