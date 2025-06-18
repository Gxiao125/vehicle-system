#include "../include/CANService.h"
#include <iostream>
#include <thread>
#include <csignal>
#include <cstdlib>
#include <memory>

int main(int argc, char* argv[]) {
    // 默认参数
    std::string device_path = "/dev/can0";
    std::string shm_name = "/can_shared_mem";
    size_t shm_pool_size = 64;
    
    // 解析命令行参数
    if (argc > 1) device_path = argv[1];
    if (argc > 2) shm_name = argv[2];
    if (argc > 3) shm_pool_size = std::stoul(argv[3]);
    
    try {
        // 创建CAN服务
        CANService service(device_path, shm_name, shm_pool_size);
        
        // 注册消息处理器（可选）
        service.registerMessageHandler([](uint32_t can_id, const std::vector<uint8_t>& data) {
            std::cout << "Received message: CAN ID=0x" << std::hex << can_id << std::dec
                      << ", Length=" << data.size() << std::endl;
        });
        
        // 启动服务
        service.start();
        
        // 主循环
        while (service.isRunning()) {
            // 可以在此添加服务监控逻辑
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
        
        // 停止服务
        service.stop();
        
        return EXIT_SUCCESS;
        
    } catch (const std::exception& e) {
        std::cerr << "CANService fatal error: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }
}
