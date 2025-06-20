#include "VehicleCommunicationAPI.h"
#include <iostream>
#include <thread>
#include <ncurses.h> // 用于控制台显示

void vehicleStatusHandler(const std::string& name, double value, const std::string& unit) {
    // 实际实现会在UI中更新
}

void warningHandler(const std::string& name, double value, const std::string& unit) {
    if (value > 0) {
        std::cout << "[!] WARNING: " << name << " activated!" << std::endl;
    }
}

void doorStatusHandler(const std::string& name, double value, const std::string& unit) {
    if (value > 0) {
        std::cout << "[!] Door Open: " << name << std::endl;
    }
}

void errorHandler(uint32_t can_id, const std::string& error_msg) {
    std::cerr << "[Dashboard] ERROR (ID:0x" << std::hex << can_id 
              << "): " << error_msg << std::dec << std::endl;
}

void updateDashboardDisplay(const VehicleCommunicationAPI& api) {
    // 这里使用ncurses库实现更复杂的UI
    // 简化版只打印关键信息
    std::cout << "\n=== VEHICLE DASHBOARD ===" << std::endl;
    std::cout << "Speed: 120 km/h" << std::endl;
    std::cout << "RPM: 3200" << std::endl;
    std::cout << "Fuel: 65%" << std::endl;
    std::cout << "Gear: D4" << std::endl;
    std::cout << "Warnings: None" << std::endl;
    std::cout << "Doors: All closed" << std::endl;
    std::cout << "=========================\n" << std::endl;
}

int main() {
    VehicleCommunicationAPI api("/vehicle_bus");
    api.loadMessageDefinitions("Dashboard.dbc");
    
    // 注册信号处理回调
    api.registerSignalHandler("SPEED", vehicleStatusHandler);
    api.registerSignalHandler("RPM", vehicleStatusHandler);
    api.registerSignalHandler("FUEL_LEVEL", vehicleStatusHandler);
    api.registerSignalHandler("GEAR_POS", vehicleStatusHandler);
    
    api.registerSignalHandler("CHECK_ENGINE", warningHandler);
    api.registerSignalHandler("OIL_PRESSURE", warningHandler);
    api.registerSignalHandler("BATTERY", warningHandler);
    
    api.registerSignalHandler("DRIVER_DOOR", doorStatusHandler);
    api.registerSignalHandler("PASSENGER_DOOR", doorStatusHandler);
    api.registerSignalHandler("TRUNK", doorStatusHandler);
    
    api.registerErrorHandler(errorHandler);
    
    api.start();
    
    std::cout << "Dashboard System Started" << std::endl;
    
    while (true) {
        // 更新仪表盘显示
        updateDashboardDisplay(api);
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    
    return 0;
}
