#include "VehicleCommunicationAPI.h"
#include <iostream>
#include <thread>
#include <random>
#include <string>

// 修正后的回调函数 - 直接使用 double 值
void doorStatusHandler(const std::string& signal_name, double value, const std::string& unit) {
    // value 已经是 double 类型，直接使用
    bool status = (value > 0.5);
    std::cout << "[BodyControl] Door Status: " << signal_name 
              << " = " << (status ? "OPEN" : "CLOSED") << std::endl;
}

void windowStatusHandler(const std::string& signal_name, double value, const std::string& unit) {
    // value 直接是车窗位置值
    std::cout << "[BodyControl] Window: " << signal_name 
              << " = " << value << unit << std::endl;
}

void lightingHandler(const std::string& signal_name, double value, const std::string& unit) {
    // value 直接是灯光状态值
    bool status = (value > 0.5);
    std::cout << "[BodyControl] Lighting: " << signal_name 
              << " = " << (status ? "ON" : "OFF") << std::endl;
}

// 错误处理函数
void errorHandler(uint32_t can_id, const std::string& error_msg) {
    std::cerr << "[BodyControl] ERROR (ID:0x" << std::hex << can_id 
              << "): " << error_msg << std::dec << std::endl;
}

int main() {
    VehicleCommunicationAPI api("/can_shared_mem");
    
    // 加载 DBC 文件
    api.loadMessageDefinitions("/app/body_control/BodyControl.dbc");
    
    // 修正 lambda 表达式 - 使用正确的参数类型
    api.registerSignalHandler("DRIVER_DOOR", [](const std::string& n, double v, const std::string& u) {
        doorStatusHandler(n, v, u);
    });
    
    api.registerSignalHandler("PASSENGER_DOOR", [](const std::string& n, double v, const std::string& u) {
        doorStatusHandler(n, v, u);
    });
    
    api.registerSignalHandler("TRUNK", [](const std::string& n, double v, const std::string& u) {
        doorStatusHandler(n, v, u);
    });
    
    api.registerSignalHandler("HOOD", [](const std::string& n, double v, const std::string& u) {
        doorStatusHandler(n, v, u);
    });
    
    api.registerSignalHandler("DRIVER_WINDOW", [](const std::string& n, double v, const std::string& u) {
        windowStatusHandler(n, v, u);
    });
    
    api.registerSignalHandler("PASSENGER_WINDOW", [](const std::string& n, double v, const std::string& u) {
        windowStatusHandler(n, v, u);
    });
    
    api.registerSignalHandler("HEADLIGHTS", [](const std::string& n, double v, const std::string& u) {
        lightingHandler(n, v, u);
    });
    
    api.registerSignalHandler("BRAKE_LIGHTS", [](const std::string& n, double v, const std::string& u) {
        lightingHandler(n, v, u);
    });
    
    api.registerSignalHandler("INDICATOR_LEFT", [](const std::string& n, double v, const std::string& u) {
        lightingHandler(n, v, u);
    });
    
    api.registerSignalHandler("INDICATOR_RIGHT", [](const std::string& n, double v, const std::string& u) {
        lightingHandler(n, v, u);
    });
    
    api.registerErrorHandler(errorHandler);
    
    api.start();
    
    std::cout << "Body Control System Started" << std::endl;
    
    // 模拟车门状态变化
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<int> doorDist(0, 1);
    
    while (true) {
        try {
            // 发送信号 - 使用 double 值
            api.sendSignal("DRIVER_DOOR", static_cast<double>(doorDist(gen)));
            api.sendSignal("PASSENGER_DOOR", static_cast<double>(doorDist(gen)));
            
            std::this_thread::sleep_for(std::chrono::seconds(5));
        } catch (const std::exception& e) {
            std::cerr << "Error sending signal: " << e.what() << std::endl;
        } catch (...) {
            std::cerr << "Unknown error sending signal" << std::endl;
        }
    }
    
    return 0;
}
