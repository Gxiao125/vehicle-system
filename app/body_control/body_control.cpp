#include "VehicleCommunicationAPI.h"
#include <iostream>
#include <thread>
#include <cctype>
#include <termios.h>
#include <unistd.h>
#include <vector>
#include <atomic>
#include <csignal>
#include <unordered_map>
#include <mutex>
#include <cmath>  // 添加cmath用于fabs函数
#include <cstring>  // 添加cstring用于strstr函数

// 全局变量用于控制程序退出
std::atomic<bool> g_running(true);

// 修改终端设置以实现非阻塞键盘输入
struct termios orig_termios;
void disableRawMode() {
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
}

void enableRawMode() {
    tcgetattr(STDIN_FILENO, &orig_termios);
    atexit(disableRawMode);
    
    struct termios raw = orig_termios;
    raw.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}

// 信号处理函数
void signalHandler(int signum) {
    if (signum == SIGINT) {
        g_running = false;
        std::cout << "\nExiting program..." << std::endl;
    }
}

// 全局状态变化打印函数 - 只打印变化的值
void statusChangePrinter(const std::string& signal_name, double value, const std::string& unit) {
    static std::unordered_map<std::string, double> last_values;
    static std::mutex mtx;
    const double threshold = 0.01;  // 变化阈值

    std::lock_guard<std::mutex> lock(mtx);
    
    // 检查是否需要更新
    auto it = last_values.find(signal_name);
    if (it == last_values.end() || std::fabs(it->second - value) > threshold) {
        last_values[signal_name] = value;
        
        // 确定显示格式
        std::string display_value;
        
        // 门状态显示
        if (strstr(signal_name.c_str(), "DOOR") != nullptr || 
            strstr(signal_name.c_str(), "TRUNK") != nullptr || 
            strstr(signal_name.c_str(), "HOOD") != nullptr) {
            display_value = (value > 0.5) ? "OPEN" : "CLOSED";
        } 
        // 车窗状态显示
        else if (strstr(signal_name.c_str(), "WINDOW") != nullptr) {
            display_value = std::to_string(static_cast<int>(value)) + unit;
        } 
        // 灯光状态显示
        else if (strstr(signal_name.c_str(), "LIGHTS") != nullptr || 
                 strstr(signal_name.c_str(), "INDICATOR") != nullptr) {
            display_value = (value > 0.5) ? "ON" : "OFF";
        } 
        // 其他信号
        else {
            display_value = std::to_string(value) + unit;
        }
        
        std::cout << "[BodyControl] " << signal_name << "=" << display_value << std::endl;
    }
}

// 错误处理函数
void errorHandler(uint32_t can_id, const std::string& error_msg) {
    // 错误处理保持不变
    // std::cerr << "[BodyControl] ERROR (ID:0x" << std::hex << can_id 
    //           << "): " << error_msg << std::dec << std::endl;
}

// 打印控制菜单
void printControlMenu() {
    static bool first_print = true;
    
    if (first_print) {
        std::cout << "\n===== Body Control System =====\n";
        std::cout << "1. Driver Door: Toggle (d)\n";
        std::cout << "2. Passenger Door: Toggle (p)\n";
        std::cout << "3. Trunk: Toggle (t)\n";
        std::cout << "4. Hood: Toggle (h)\n";
        std::cout << "5. Driver Window: Up (u), Down (j)\n";
        std::cout << "6. Passenger Window: Up (i), Down (k)\n";
        std::cout << "7. Headlights: Toggle (1)\n";
        std::cout << "8. Brake Lights: Toggle (2)\n";
        std::cout << "9. Left Indicator: Toggle (3)\n";
        std::cout << "0. Right Indicator: Toggle (4)\n";
        std::cout << "Q. Quit (q)\n";
        std::cout << "===============================\n";
        first_print = false;
    }
    
    std::cout << "> ";
    std::cout.flush();
}

int main() {
    // 设置信号处理
    signal(SIGINT, signalHandler);
    
    VehicleCommunicationAPI api("/can_shared_mem");
    
    // 加载 DBC 文件
    api.loadMessageDefinitions("/app/body_control/BodyControl.dbc");
    
    // 注册信号处理回调 - 统一使用statusChangePrinter
    api.registerSignalHandler("DRIVER_DOOR", statusChangePrinter);
    api.registerSignalHandler("PASSENGER_DOOR", statusChangePrinter);
    api.registerSignalHandler("TRUNK", statusChangePrinter);
    api.registerSignalHandler("HOOD", statusChangePrinter);
    api.registerSignalHandler("DRIVER_WINDOW", statusChangePrinter);
    api.registerSignalHandler("PASSENGER_WINDOW", statusChangePrinter);
    api.registerSignalHandler("HEADLIGHTS", statusChangePrinter);
    api.registerSignalHandler("BRAKE_LIGHTS", statusChangePrinter);
    api.registerSignalHandler("INDICATOR_LEFT", statusChangePrinter);
    api.registerSignalHandler("INDICATOR_RIGHT", statusChangePrinter);
    
    api.registerErrorHandler(errorHandler);
    
    api.start();
    
    std::cout << "Body Control System Started" << std::endl;
    
    // 设置非阻塞输入
    enableRawMode();
    
    // 当前状态变量
    double driverDoor = 0.0;
    double passengerDoor = 0.0;
    double trunk = 0.0;
    double hood = 0.0;
    double driverWindow = 0.0;
    double passengerWindow = 0.0;
    double headlights = 0.0;
    double brakeLights = 0.0;
    double indicatorLeft = 0.0;
    double indicatorRight = 0.0;
    
    // 打印初始菜单
    printControlMenu();
    
    while (g_running) {
        char c = '\0';
        if (read(STDIN_FILENO, &c, 1) > 0) {
            c = std::tolower(c);
            
            switch (c) {
                // 车门控制
                case 'd':
                    driverDoor = (driverDoor > 0.5) ? 0.0 : 1.0;
                    api.sendSignal("DRIVER_DOOR", driverDoor);
                    break;
                    
                case 'p':
                    passengerDoor = (passengerDoor > 0.5) ? 0.0 : 1.0;
                    api.sendSignal("PASSENGER_DOOR", passengerDoor);
                    break;
                    
                case 't':
                    trunk = (trunk > 0.5) ? 0.0 : 1.0;
                    api.sendSignal("TRUNK", trunk);
                    break;
                    
                case 'h':
                    hood = (hood > 0.5) ? 0.0 : 1.0;
                    api.sendSignal("HOOD", hood);
                    break;
                    
                // 车窗控制
                case 'u':
                    driverWindow = std::min(100.0, driverWindow + 10.0);
                    api.sendSignal("DRIVER_WINDOW", driverWindow);
                    break;
                    
                case 'j':
                    driverWindow = std::max(0.0, driverWindow - 10.0);
                    api.sendSignal("DRIVER_WINDOW", driverWindow);
                    break;
                    
                case 'i':
                    passengerWindow = std::min(100.0, passengerWindow + 10.0);
                    api.sendSignal("PASSENGER_WINDOW", passengerWindow);
                    break;
                    
                case 'k':
                    passengerWindow = std::max(0.0, passengerWindow - 10.0);
                    api.sendSignal("PASSENGER_WINDOW", passengerWindow);
                    break;
                    
                // 灯光控制
                case '1':
                    headlights = (headlights > 0.5) ? 0.0 : 1.0;
                    api.sendSignal("HEADLIGHTS", headlights);
                    break;
                    
                case '2':
                    brakeLights = (brakeLights > 0.5) ? 0.0 : 1.0;
                    api.sendSignal("BRAKE_LIGHTS", brakeLights);
                    break;
                    
                case '3':
                    indicatorLeft = (indicatorLeft > 0.5) ? 0.0 : 1.0;
                    api.sendSignal("INDICATOR_LEFT", indicatorLeft);
                    break;
                    
                case '4':
                    indicatorRight = (indicatorRight > 0.5) ? 0.0 : 1.0;
                    api.sendSignal("INDICATOR_RIGHT", indicatorRight);
                    break;
                    
                // 退出程序
                case 'q':
                    g_running = false;
                    std::cout << "Exiting program..." << std::endl;
                    break;
                    
                default:
                    // 只打印未知命令
                    std::cout << "Unknown command: " << c << std::endl;
                    printControlMenu();
                    continue;
            }
            
            // 重新打印提示符
            if (g_running) {
                printControlMenu();
            }
        }
        
        // 稍微休眠以减少CPU占用
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    
    // 清理工作
    disableRawMode();
    api.stop();
    
    return 0;
}
