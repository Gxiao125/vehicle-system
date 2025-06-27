#include "../include/CANService.h"
#include <iostream>
#include <thread>
#include <stdexcept>

// 初始化静态成员
std::atomic<bool> CANService::global_running_(true);

void CANService::signalHandler(int signal) {
    if (signal == SIGINT || signal == SIGTERM) {
        global_running_.store(false);
    }
}

CANService::CANService(const std::string& device_path, 
                     const std::string& shm_name,
                     size_t shm_pool_size)
    : can_controller_(device_path),
      transport_layer_(can_controller_, shm_name, shm_pool_size) {}

void CANService::start() {
    if (running_) return;
    
    // 注册信号处理器
    std::signal(SIGINT, CANService::signalHandler);
    std::signal(SIGTERM, CANService::signalHandler);
    
    try {
        // 初始化CAN控制器
        if (!can_controller_.init()) {
            throw std::runtime_error("CAN controller initialization failed");
        }
        
        // 启动系统
        can_controller_.start();

        transport_layer_.start();

        
        running_ = true;
        global_running_.store(true);
        
        std::cout << "CANService started successfully." << std::endl;
        std::cout << "  Device: " << can_controller_.getDevicePath() << std::endl;
        // std::cout << "  Shared memory: " << transport_layer_.getShmName() << std::endl;
        std::cout << "Press Ctrl+C to stop..." << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Failed to start CANService: " << e.what() << std::endl;
        stop();
        throw;
    }
}

void CANService::stop() {
    if (!running_) return;
    
    // 停止传输层
    transport_layer_.stop();
    
    // 停止CAN控制器
    can_controller_.stop();
    
    running_ = false;
    global_running_.store(false);
    
    std::cout << "CANService stopped." << std::endl;
}

void CANService::registerMessageHandler(
    std::function<void(uint32_t, const std::vector<uint8_t>&)> handler) {
    transport_layer_.registerMessageHandler(handler);
}
