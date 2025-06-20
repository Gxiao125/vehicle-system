#include "VehicleCommunicationAPI.h"
#include <iostream>
#include <thread>
#include <random>

void doorStatusHandler(const std::string& name, double value, const std::string& unit) {
    std::cout << "[BodyControl] Door Status: " << name << " = " << (value ? "OPEN" : "CLOSED") << std::endl;
}

void windowStatusHandler(const std::string& name, double value, const std::string& unit) {
    std::cout << "[BodyControl] Window: " << name << " = " << value << unit << std::endl;
}

void lightingHandler(const std::string& name, double value, const std::string& unit) {
    std::cout << "[BodyControl] Lighting: " << name << " = " << (value ? "ON" : "OFF") << std::endl;
}

void errorHandler(uint32_t can_id, const std::string& error_msg) {
    std::cerr << "[BodyControl] ERROR (ID:0x" << std::hex << can_id 
              << "): " << error_msg << std::dec << std::endl;
}

int main() {
    VehicleCommunicationAPI api("/vehicle_bus");
    api.loadMessageDefinitions("BodyControl.dbc");
    
    // 注册信号处理回调
    api.registerSignalHandler("DRIVER_DOOR", doorStatusHandler);
    api.registerSignalHandler("PASSENGER_DOOR", doorStatusHandler);
    api.registerSignalHandler("TRUNK", doorStatusHandler);
    api.registerSignalHandler("HOOD", doorStatusHandler);
    
    api.registerSignalHandler("DRIVER_WINDOW", windowStatusHandler);
    api.registerSignalHandler("PASSENGER_WINDOW", windowStatusHandler);
    
    api.registerSignalHandler("HEADLIGHTS", lightingHandler);
    api.registerSignalHandler("BRAKE_LIGHTS", lightingHandler);
    api.registerSignalHandler("INDICATOR_LEFT", lightingHandler);
    api.registerSignalHandler("INDICATOR_RIGHT", lightingHandler);
    
    api.registerErrorHandler(errorHandler);
    
    api.start();
    
    std::cout << "Body Control System Started" << std::endl;
    
    // 模拟车门状态变化
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> doorDist(0, 1);
    
    while (true) {
        // 随机切换车门状态
        api.sendSignal("DRIVER_DOOR", doorDist(gen));
        api.sendSignal("PASSENGER_DOOR", doorDist(gen));
        
        std::this_thread::sleep_for(std::chrono::seconds(5));
    }
    
    return 0;
}
