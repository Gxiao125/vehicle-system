#include "VehicleCommunicationAPI.h"
#include <iostream>
#include <thread>
#include <cmath>

void engineDataHandler(const std::string& name, double value, const std::string& unit) {
    std::cout << "[Powertrain] " << name << ": " << value << " " << unit << std::endl;
}

void gearHandler(const std::string& name, double value, const std::string& unit) {
    if (name == "CURRENT_GEAR") {
        std::cout << "[Powertrain] Current Gear: " << static_cast<int>(value) << std::endl;
    } else if (name == "GEAR_MODE") {
        const char* modes[] = {"P", "R", "N", "D"};
        std::cout << "[Powertrain] Gear Mode: " << modes[static_cast<int>(value)] << std::endl;
    }
}

void fuelHandler(const std::string& name, double value, const std::string& unit) {
    std::cout << "[Powertrain] " << name << ": " << value << " " << unit << std::endl;
}

void errorHandler(uint32_t can_id, const std::string& error_msg) {
    std::cerr << "[Powertrain] ERROR (ID:0x" << std::hex << can_id 
              << "): " << error_msg << std::dec << std::endl;
}

int main() {
    VehicleCommunicationAPI api("/vehicle_bus");
    api.loadMessageDefinitions("Powertrain.dbc");
    
    // 注册信号处理回调
    api.registerSignalHandler("RPM", engineDataHandler);
    api.registerSignalHandler("SPEED", engineDataHandler);
    api.registerSignalHandler("COOLANT_TEMP", engineDataHandler);
    api.registerSignalHandler("THROTTLE_POS", engineDataHandler);
    
    api.registerSignalHandler("CURRENT_GEAR", gearHandler);
    api.registerSignalHandler("GEAR_MODE", gearHandler);
    
    api.registerSignalHandler("FUEL_LEVEL", fuelHandler);
    api.registerSignalHandler("FUEL_CONSUMPTION", fuelHandler);
    
    api.registerErrorHandler(errorHandler);
    
    api.start();
    
    std::cout << "Powertrain System Started" << std::endl;
    
    // 模拟引擎数据变化
    double rpm = 800;
    double speed = 0;
    int gear = 0;
    double throttle = 0;
    
    while (true) {
        // 更新引擎数据
        rpm = 800 + 200 * sin(speed/10);
        if (rpm > 7000) rpm = 7000;
        
        api.sendSignal("RPM", rpm);
        api.sendSignal("SPEED", speed);
        api.sendSignal("COOLANT_TEMP", 90 + 10 * sin(speed/5));
        api.sendSignal("THROTTLE_POS", throttle);
        
        // 更新变速箱状态
        if (speed < 20) gear = 1;
        else if (speed < 40) gear = 2;
        else if (speed < 60) gear = 3;
        else gear = 4;
        
        api.sendSignal("CURRENT_GEAR", gear);
        api.sendSignal("GEAR_MODE", 3); // D模式
        
        // 更新燃油系统
        api.sendSignal("FUEL_LEVEL", 80 - speed/5);
        api.sendSignal("FUEL_CONSUMPTION", 5 + 5 * sin(speed/10));
        
        // 增加速度
        speed += 0.5;
        if (speed > 120) speed = 0;
        
        // 随机油门变化
        throttle = 10 + 30 * sin(speed/5);
        
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    return 0;
}
