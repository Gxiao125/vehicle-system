#include "VehicleCommunicationAPI.h"
#include <iostream>
#include <thread>
#include <cmath>
#include <mutex>
#include <fstream>
#include <unistd.h>
#include <chrono>
#include <algorithm>
#include <iomanip>
#include <vector>

// 互斥锁保护共享数据
std::mutex data_mutex;
double rpm = 800;
double speed_kmh = 0;
int gear = 0;
double throttle = 0;
double fuel_level = 80;
double coolant_temp = 90;
double fuel_consumption = 7.5;

// ADC文件路径
const char* ADC_FILE = "/sys/bus/iio/devices/iio:device0/in_voltage1_raw";

// 读取ADC值的函数
double readADC() {
    std::ifstream adc_file(ADC_FILE);
    if (!adc_file.is_open()) {
        std::cerr << "Failed to open ADC file: " << ADC_FILE << std::endl;
        return 0.0;
    }
    
    int adc_value;
    adc_file >> adc_value;
    adc_file.close();
    
    // 假设ADC是12位（0-4095），转换为0-100%的油门开度
    double result = (adc_value / 4095.0) * 100.0;
    return result;
}

// 信号处理回调
void engineDataHandler(const std::string& name, double value, const std::string& unit) {
    // 禁用详细日志以减少输出
}

void gearHandler(const std::string& name, double value, const std::string& unit) {
    // 禁用详细日志以减少输出
}

void fuelHandler(const std::string& name, double value, const std::string& unit) {
    // 禁用详细日志以减少输出
}

void errorHandler(uint32_t can_id, const std::string& error_msg) {
    // std::cerr << "[ERROR] CAN ID:0x" << std::hex << can_id 
    //           << " - " << error_msg << std::dec << std::endl;
}

template<typename T>
T clamp_value(T value, T min_val, T max_val) {
    if (value < min_val) return min_val;
    if (value > max_val) return max_val;
    return value;
}

// CAN发送线程函数
void canSenderThread(VehicleCommunicationAPI* api) {
    while (true) {
        double current_rpm;
        double current_speed;
        int current_gear;
        double current_throttle;
        double current_fuel_level;
        double current_coolant_temp;
        double current_fuel_consumption;
        
        // 获取当前状态值
        {
            std::lock_guard<std::mutex> lock(data_mutex);
            current_rpm = rpm;
            current_speed = speed_kmh;
            current_gear = gear;
            current_throttle = throttle;
            current_fuel_level = fuel_level;
            current_coolant_temp = coolant_temp;
            current_fuel_consumption = fuel_consumption;
        }
        
        try {
            // 1. 发送发动机数据 (ID 100)
            std::unordered_map<std::string, CanMessageParser::SignalValue> engine_signals;
            // RPM: 直接传递物理值 [0-8000] rpm
            engine_signals["RPM"] = clamp_value(current_rpm, 0.0, 8000.0);
            
            // SPEED: 直接传递物理值 [0-300] km/h
            engine_signals["SPEED"] = clamp_value(current_speed, 0.0, 300.0);
            
            // COOLANT_TEMP: 直接传递物理值 [-40-215]°C
            engine_signals["COOLANT_TEMP"] = clamp_value(current_coolant_temp, -40.0, 215.0);
            
            // THROTTLE_POS: 直接传递物理值 [0-100]%
            engine_signals["THROTTLE_POS"] = clamp_value(current_throttle, 0.0, 100.0);
            
            api->sendMultipleSignals(100, engine_signals);

            // 2. 发送变速箱数据 (ID 101)
            std::unordered_map<std::string, CanMessageParser::SignalValue> gear_signals;
            gear_signals["CURRENT_GEAR"] = static_cast<CanMessageParser::SignalValue>(static_cast<int64_t>(clamp_value(current_gear, 0, 7)));
            gear_signals["GEAR_MODE"] = static_cast<CanMessageParser::SignalValue>(static_cast<int64_t>(3));
            
            api->sendMultipleSignals(101, gear_signals);

            // 3. 发送燃油系统数据 (ID 102)
            std::unordered_map<std::string, CanMessageParser::SignalValue> fuel_signals;
            // FUEL_LEVEL: 直接传递物理值 [0-100]%
            fuel_signals["FUEL_LEVEL"] = clamp_value(current_fuel_level, 0.0, 100.0);
            
            // FUEL_CONSUMPTION: 直接传递物理值 [0-100] L/100km
            fuel_signals["FUEL_CONSUMPTION"] = clamp_value(current_fuel_consumption, 0.0, 100.0);
            
            api->sendMultipleSignals(102, fuel_signals);
            
            // 添加发送日志（每秒一次）
            static std::chrono::steady_clock::time_point last_log = std::chrono::steady_clock::now();
            auto now = std::chrono::steady_clock::now();
            if (std::chrono::duration_cast<std::chrono::seconds>(now - last_log).count() >= 1) {
                std::cout << "Sent: "
                          << "RPM=" << current_rpm << ", "
                          << "SPEED=" << current_speed << "km/h, "
                          << "COOLANT=" << current_coolant_temp << "°C, "
                          << "THROTTLE=" << current_throttle << "%, "
                          << "GEAR=" << current_gear << std::endl;
                last_log = now;
            }
            
        } catch (const std::exception& e) {
            std::cerr << "Error sending CAN data: " << e.what() << std::endl;
        }
        
        // 控制发送频率 (20Hz)
        usleep(50000);
    }
}

int main() {
    VehicleCommunicationAPI api("/can_shared_mem");
    api.loadMessageDefinitions("/app/brake_system/Powertrain.dbc");

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
    
    // 启动API
    api.start();
    
    std::cout << "Powertrain System Started (Potentiometer Throttle)" << std::endl;
    
    // 启动CAN发送线程
    std::thread canThread(canSenderThread, &api);
    
    // 车辆动力学参数
    const double max_rpm = 7000.0;
    const double idle_rpm = 800.0;
    const double max_speed_kmh = 200.0;
    const double drag_coeff = 0.35;
    const double rolling_resistance = 0.015;
    const double vehicle_mass = 1500.0;
    const double wheel_radius = 0.3;
    const double gear_ratios[] = {0.0, 3.5, 2.5, 1.8, 1.3, 1.0};
    const double final_drive_ratio = 4.1;
    
    // 上次循环时间点
    std::chrono::steady_clock::time_point last_time = std::chrono::steady_clock::now();
    std::chrono::steady_clock::time_point last_fuel_update = last_time;
    std::chrono::steady_clock::time_point last_debug_output = last_time;
    
    // 添加启动辅助力计数器
    const double stall_assist_force = 800.0;
    double additional_force = 0.0;
    
    while (true) {
        // 计算时间差（秒）
        std::chrono::steady_clock::time_point current_time = std::chrono::steady_clock::now();
        std::chrono::duration<double> time_diff = current_time - last_time;
        double delta_time = time_diff.count();
        last_time = current_time;
        
        // 读取ADC值作为油门输入
        double throttle_input = readADC();
        
        // 更新油门位置（平滑变化）
        {
            std::lock_guard<std::mutex> lock(data_mutex);
            throttle = 0.8 * throttle + 0.2 * throttle_input;
            throttle = clamp_value(throttle, 0.0, 100.0);  // 确保油门在0-100%范围内
        }
        
        // 1. 单位转换：km/h → m/s
        double current_speed;
        {
            std::lock_guard<std::mutex> lock(data_mutex);
            current_speed = speed_kmh;
        }
        double speed_mps = current_speed / 3.6;
        
        // 2. 根据挡位计算传动比
        int current_gear;
        {
            std::lock_guard<std::mutex> lock(data_mutex);
            current_gear = gear;
        }
        double transmission_ratio = 0.0;
        if (current_gear >= 1 && current_gear <= 5) {
            transmission_ratio = gear_ratios[current_gear] * final_drive_ratio;
        }
        
        // 3. 计算发动机扭矩
        double max_torque = 300.0;
        double current_throttle;
        {
            std::lock_guard<std::mutex> lock(data_mutex);
            current_throttle = throttle;
        }
        double engine_torque = max_torque * (current_throttle / 100.0);
        
        // 4. 计算车轮扭矩
        const double efficiency = 0.85;
        double wheel_torque = engine_torque * transmission_ratio * efficiency;
        
        // 5. 计算驱动力
        double drive_force = wheel_torque / wheel_radius;
        
        // 6. 启动辅助
        if (current_speed < 5.0 && current_throttle > 10.0) {
            additional_force = stall_assist_force * (current_throttle / 100.0);
        } else {
            additional_force = 0.0;
        }
        
        // 7. 计算阻力
        double air_resistance = 0.5 * drag_coeff * 1.2 * 2.2 * pow(speed_mps, 2);
        double rolling_resistance_force = rolling_resistance * vehicle_mass * 9.8;
        double total_resistance = air_resistance + rolling_resistance_force;
        
        // 8. 计算加速度
        double acceleration = (drive_force + additional_force - total_resistance) / vehicle_mass;
        
        // 9. 更新速度（m/s）
        speed_mps += acceleration * delta_time;
        
        // 10. 转换回km/h
        double new_speed_kmh = speed_mps * 3.6;
        new_speed_kmh = clamp_value(new_speed_kmh, 0.0, max_speed_kmh);
        {
            std::lock_guard<std::mutex> lock(data_mutex);
            speed_kmh = new_speed_kmh;
        }
        
        // 11. 根据速度更新挡位
        int new_gear = 0;
        if (new_speed_kmh < 1.0) {
            new_gear = 0;
        } else if (new_speed_kmh < 20.0) {
            new_gear = 1;
        } else if (new_speed_kmh < 40.0) {
            new_gear = 2;
        } else if (new_speed_kmh < 60.0) {
            new_gear = 3;
        } else if (new_speed_kmh < 80.0) {
            new_gear = 4;
        } else {
            new_gear = 5;
        }
        {
            std::lock_guard<std::mutex> lock(data_mutex);
            gear = new_gear;
        }
        
        // 12. 计算RPM（确保不超过7000）
        double new_rpm = idle_rpm;
        if (transmission_ratio > 0.01) {
            // 正确的RPM计算公式
            new_rpm = (speed_mps * transmission_ratio * 60.0) / 
                      (2 * M_PI * wheel_radius);
        } else {
            new_rpm = idle_rpm + (current_throttle * 20);
        }
        new_rpm = clamp_value(new_rpm, idle_rpm, max_rpm);
        {
            std::lock_guard<std::mutex> lock(data_mutex);
            rpm = new_rpm;
        }
        
        // 13. 更新冷却液温度
        double new_coolant_temp = 85.0 + (new_rpm / 100.0) + (new_speed_kmh / 50.0);
        new_coolant_temp = clamp_value(new_coolant_temp, 70.0, 110.0);
        {
            std::lock_guard<std::mutex> lock(data_mutex);
            coolant_temp = new_coolant_temp;
        }
        
        // 14. 更新燃油系统（确保油位不超过100%）
        std::chrono::duration<double> fuel_time_diff = current_time - last_fuel_update;
        
        if (fuel_time_diff.count() >= 1) {
            double current_fuel;
            {
                std::lock_guard<std::mutex> lock(data_mutex);
                current_fuel = fuel_level;
            }
            double fuel_rate = (0.0001 * new_rpm) + (0.0002 * new_speed_kmh) + (0.0005 * current_throttle);
            double new_fuel_level = current_fuel - fuel_rate * fuel_time_diff.count();
            
            // 确保燃油油位在0-100%范围内
            new_fuel_level = clamp_value(new_fuel_level, 0.0, 100.0);
            
            {
                std::lock_guard<std::mutex> lock(data_mutex);
                fuel_level = new_fuel_level;
            }
            
            double new_fuel_consumption = 0.0;
            if (new_speed_kmh > 5) {
                new_fuel_consumption = (fuel_rate * 360000.0) / (new_speed_kmh * 100.0);
            }
            {
                std::lock_guard<std::mutex> lock(data_mutex);
                fuel_consumption = new_fuel_consumption;
            }
            
            last_fuel_update = current_time;
        }
        
        // 调试输出（每秒一次）
        std::chrono::duration<double> debug_diff = current_time - last_debug_output;
        if (debug_diff.count() > 1.0) {
            double current_throttle_val, current_gear_val, current_speed_val, current_rpm_val, current_fuel_val;
            {
                std::lock_guard<std::mutex> lock(data_mutex);
                current_throttle_val = throttle;
                current_gear_val = gear;
                current_speed_val = speed_kmh;
                current_rpm_val = rpm;
                current_fuel_val = fuel_level;
            }
            
            std::cout << "状态更新: "
                      << "油门=" << current_throttle_val << "%, "
                      << "挡位=" << current_gear_val << ", "
                      << "传动比=" << transmission_ratio << ", "
                      << "驱动力=" << drive_force << "N, "
                      << "辅助力=" << additional_force << "N, "
                      << "阻力=" << total_resistance << "N, "
                      << "加速度=" << acceleration << "m/s², "
                      << "速度=" << current_speed_val << "km/h, "
                      << "RPM=" << current_rpm_val << ", "
                      << "油量=" << current_fuel_val << "%"
                      << std::endl;
            last_debug_output = current_time;
        }
        
        // 控制循环频率 (20Hz)
        usleep(50000);
    }
    
    api.stop();
    return 0;
}
