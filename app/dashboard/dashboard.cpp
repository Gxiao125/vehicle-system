#include "VehicleCommunicationAPI.h"
#include <iostream>
#include <thread>
#include <mutex>
#include <map>
#include <string>
#include <cmath>
#include <cstdio>
#include <csignal>
#include <cstdlib>
#include <unistd.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <vector>
#include <algorithm>
#include <chrono>

// 线程安全的信号值存储
struct SignalData {
    double value;
    std::string unit;
};

std::mutex g_data_mutex;
std::map<std::string, SignalData> g_signal_values;

// 帧缓冲区状态
struct FramebufferInfo {
    int fbfd = 0;
    char* fbp = nullptr;
    struct fb_fix_screeninfo finfo;
    struct fb_var_screeninfo vinfo;
    long int screensize = 0;
    int bpp = 16; // 默认16位色深
    int bytes_per_pixel = 2; // RGB565每像素2字节
    bool landscape = true; // 横屏模式
};

FramebufferInfo fb_info;
bool use_framebuffer = false;

// 初始化帧缓冲区
bool init_framebuffer() {
    fb_info.fbfd = open("/dev/fb0", O_RDWR);
    if (fb_info.fbfd == -1) {
        perror("Error opening framebuffer device");
        return false;
    }

    if (ioctl(fb_info.fbfd, FBIOGET_FSCREENINFO, &fb_info.finfo)) {
        perror("Error reading fixed information");
        close(fb_info.fbfd);
        return false;
    }

    if (ioctl(fb_info.fbfd, FBIOGET_VSCREENINFO, &fb_info.vinfo)) {
        perror("Error reading variable information");
        close(fb_info.fbfd);
        return false;
    }
    
    // 获取实际像素格式
    fb_info.bpp = fb_info.vinfo.bits_per_pixel;
    fb_info.bytes_per_pixel = (fb_info.bpp + 7) / 8;
    
    std::cout << "Framebuffer info: "
              << fb_info.vinfo.xres << "x" << fb_info.vinfo.yres
              << ", " << fb_info.bpp << "bpp"
              << ", line length: " << fb_info.finfo.line_length << " bytes\n";

    // 检测屏幕方向 (横屏/竖屏)
    fb_info.landscape = fb_info.vinfo.xres > fb_info.vinfo.yres;
    std::cout << "Screen orientation: " << (fb_info.landscape ? "Landscape" : "Portrait") << std::endl;

    fb_info.screensize = fb_info.vinfo.yres * fb_info.finfo.line_length;
    fb_info.fbp = (char*)mmap(0, fb_info.screensize, PROT_READ | PROT_WRITE, MAP_SHARED, fb_info.fbfd, 0);
    
    if (fb_info.fbp == MAP_FAILED) {
        perror("Error mapping framebuffer to memory");
        close(fb_info.fbfd);
        return false;
    }

    return true;
}

// 关闭帧缓冲区
void close_framebuffer() {
    if (fb_info.fbp) {
        munmap(fb_info.fbp, fb_info.screensize);
    }
    if (fb_info.fbfd) {
        close(fb_info.fbfd);
    }
}

// 统一信号处理函数
void signalHandler(const std::string& name, double value, const std::string& unit) {
    std::lock_guard<std::mutex> lock(g_data_mutex);
    g_signal_values[name] = {value, unit};
}

void errorHandler(uint32_t can_id, const std::string& error_msg) {
    std::cerr << "[Dashboard] ERROR (ID:0x" << std::hex << can_id 
              << "): " << error_msg << std::dec << std::endl;
}

// 档位值转换
std::string gearToString(double value) {
    int gear = static_cast<int>(std::round(value));
    switch (gear) {
        case 0: return "P";
        case 1: return "R";
        case 2: return "N";
        case 3: return "D";
        case 4: return "D4";
        default: return "UNKNOWN";
    }
}

// 格式化警告和门状态名称
std::string formatName(const std::string& name) {
    if (name == "CHECK_ENGINE") return "Engine";
    if (name == "OIL_PRESSURE") return "Oil";
    if (name == "BATTERY") return "Battery";
    if (name == "DRIVER_DOOR") return "Driver";
    if (name == "PASSENGER_DOOR") return "Passenger";
    if (name == "TRUNK") return "Trunk";
    return name;
}

// 全局标志，用于控制主循环
volatile sig_atomic_t g_running = 1;

// 信号处理函数
void sigHandler(int signum) {
    g_running = 0;
}

// RGB565颜色转换
uint16_t rgb888_to_rgb565(uint32_t color) {
    uint8_t r = (color >> 16) & 0xFF;
    uint8_t g = (color >> 8) & 0xFF;
    uint8_t b = color & 0xFF;
    
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

// 在帧缓冲区上绘制像素
void drawPixel(int x, int y, uint32_t color) {
    if (!use_framebuffer || !fb_info.fbp) return;
    
    // 根据屏幕方向调整坐标
    int phys_x = x;
    int phys_y = y;
    
    if (fb_info.landscape) {
        // 横屏模式 - 无需调整
    } else {
        // 竖屏模式 - 旋转90度
        int temp = x;
        phys_x = y;
        phys_y = fb_info.vinfo.yres - 1 - temp;
    }
    
    if (phys_x < 0 || phys_x >= fb_info.vinfo.xres || phys_y < 0 || phys_y >= fb_info.vinfo.yres) return;
    
    // 计算位置
    long int location = (phys_y * fb_info.finfo.line_length) + (phys_x * fb_info.bytes_per_pixel);
    
    if (location < fb_info.screensize - fb_info.bytes_per_pixel) {
        if (fb_info.bpp == 16) {
            *((uint16_t*)(fb_info.fbp + location)) = rgb888_to_rgb565(color);
        } else {
            *((uint32_t*)(fb_info.fbp + location)) = color;
        }
    }
}

// 简单8x8字体数据
const uint8_t font8x8[95][8] = {
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // 空格 (32)
    {0x00, 0x00, 0x00, 0x00, 0x5F, 0x00, 0x00, 0x00}, // !
    {0x00, 0x00, 0x00, 0x03, 0x00, 0x03, 0x00, 0x00}, // "
    {0x00, 0x24, 0x7E, 0x24, 0x24, 0x7E, 0x24, 0x00}, // #
    {0x00, 0x2E, 0x2A, 0x7F, 0x2A, 0x3A, 0x00, 0x00}, // $
    {0x00, 0x46, 0x26, 0x10, 0x08, 0x64, 0x62, 0x00}, // %
    {0x00, 0x20, 0x54, 0x4A, 0x54, 0x20, 0x50, 0x00}, // &
    {0x00, 0x00, 0x00, 0x04, 0x02, 0x00, 0x00, 0x00}, // '
    {0x00, 0x00, 0x00, 0x3C, 0x42, 0x00, 0x00, 0x00}, // (
    {0x00, 0x00, 0x00, 0x42, 0x3C, 0x00, 0x00, 0x00}, // )
    {0x00, 0x10, 0x54, 0x38, 0x54, 0x10, 0x00, 0x00}, // *
    {0x00, 0x10, 0x10, 0x7C, 0x10, 0x10, 0x00, 0x00}, // +
    {0x00, 0x00, 0x00, 0x80, 0x60, 0x00, 0x00, 0x00}, // ,
    {0x00, 0x10, 0x10, 0x10, 0x10, 0x10, 0x00, 0x00}, // -
    {0x00, 0x00, 0x00, 0x60, 0x60, 0x00, 0x00, 0x00}, // .
    {0x00, 0x40, 0x20, 0x10, 0x08, 0x04, 0x00, 0x00}, // /
    {0x3C, 0x62, 0x52, 0x4A, 0x46, 0x3C, 0x00, 0x00}, // 0 (48)
    {0x00, 0x44, 0x42, 0x7E, 0x40, 0x40, 0x00, 0x00}, // 1
    {0x00, 0x64, 0x52, 0x52, 0x52, 0x4C, 0x00, 0x00}, // 2
    {0x00, 0x24, 0x42, 0x4A, 0x4A, 0x34, 0x00, 0x00}, // 3
    {0x00, 0x30, 0x28, 0x24, 0x7E, 0x20, 0x00, 0x00}, // 4
    {0x00, 0x2E, 0x4A, 0x4A, 0x4A, 0x32, 0x00, 0x00}, // 5
    {0x00, 0x3C, 0x4A, 0x4A, 0x4A, 0x30, 0x00, 0x00}, // 6
    {0x00, 0x02, 0x02, 0x62, 0x12, 0x0A, 0x06, 0x00}, // 7
    {0x00, 0x34, 0x4A, 0x4A, 0x4A, 0x34, 0x00, 0x00}, // 8
    {0x00, 0x0C, 0x52, 0x52, 0x52, 0x3C, 0x00, 0x00}, // 9
    {0x00, 0x00, 0x00, 0x48, 0x00, 0x00, 0x00, 0x00}, // :
    {0x00, 0x00, 0x80, 0x64, 0x00, 0x00, 0x00, 0x00}, // ;
    {0x00, 0x00, 0x10, 0x28, 0x44, 0x00, 0x00, 0x00}, // <
    {0x00, 0x28, 0x28, 0x28, 0x28, 0x28, 0x00, 0x00}, // =
    {0x00, 0x00, 0x44, 0x28, 0x10, 0x00, 0x00, 0x00}, // >
    {0x00, 0x04, 0x02, 0x02, 0x52, 0x0A, 0x04, 0x00}, // ?
    {0x00, 0x3C, 0x42, 0x5A, 0x56, 0x5A, 0x1C, 0x00}, // @
    {0x00, 0x7C, 0x12, 0x12, 0x12, 0x7C, 0x00, 0x00}, // A (65)
    {0x00, 0x7E, 0x4A, 0x4A, 0x4A, 0x34, 0x00, 0x00}, // B
    {0x00, 0x3C, 0x42, 0x42, 0x42, 0x24, 0x00, 0x00}, // C
    {0x00, 0x7E, 0x42, 0x42, 0x42, 0x3C, 0x00, 0x00}, // D
    {0x00, 0x7E, 0x4A, 0x4A, 0x4A, 0x42, 0x00, 0x00}, // E
    {0x00, 0x7E, 0x0A, 0x0A, 0x0A, 0x02, 0x00, 0x00}, // F
    {0x00, 0x3C, 0x42, 0x42, 0x52, 0x34, 0x00, 0x00}, // G
    {0x00, 0x7E, 0x08, 0x08, 0x08, 0x7E, 0x00, 0x00}, // H
    {0x00, 0x00, 0x42, 0x7E, 0x42, 0x00, 0x00, 0x00}, // I
    {0x00, 0x20, 0x40, 0x40, 0x40, 0x3E, 0x00, 0x00}, // J
    {0x00, 0x7E, 0x08, 0x14, 0x22, 0x40, 0x00, 0x00}, // K
    {0x00, 0x7E, 0x40, 0x40, 0x40, 0x40, 0x00, 0x00}, // L
    {0x00, 0x7E, 0x04, 0x08, 0x04, 0x7E, 0x00, 0x00}, // M
    {0x00, 0x7E, 0x04, 0x08, 0x10, 0x7E, 0x00, 0x00}, // N
    {0x00, 0x3C, 0x42, 0x42, 0x42, 0x3C, 0x00, 0x00}, // O
    {0x00, 0x7E, 0x12, 0x12, 0x12, 0x0C, 0x00, 0x00}, // P
    {0x00, 0x3C, 0x42, 0x52, 0x62, 0x3C, 0x00, 0x00}, // Q
    {0x00, 0x7E, 0x12, 0x12, 0x12, 0x6C, 0x00, 0x00}, // R
    {0x00, 0x24, 0x4A, 0x4A, 0x4A, 0x30, 0x00, 0x00}, // S
    {0x00, 0x02, 0x02, 0x7E, 0x02, 0x02, 0x00, 0x00}, // T
    {0x00, 0x3E, 0x40, 0x40, 0x40, 0x3E, 0x00, 0x00}, // U
    {0x00, 0x1E, 0x20, 0x40, 0x20, 0x1E, 0x00, 0x00}, // V
    {0x00, 0x3E, 0x40, 0x20, 0x40, 0x3E, 0x00, 0x00}, // W
    {0x00, 0x42, 0x24, 0x18, 0x24, 0x42, 0x00, 0x00}, // X
    {0x00, 0x02, 0x04, 0x78, 0x04, 0x02, 0x00, 0x00}, // Y
    {0x00, 0x42, 0x62, 0x52, 0x4A, 0x46, 0x00, 0x00}  // Z (90)
};

// 绘制字符
void drawChar(int x, int y, char c, uint32_t color) {
    if (c < 32 || c > 126) return; // 只处理可打印字符
    
    int char_index = c - 32;
    if (char_index < 0 || char_index >= 95) return;
    
    const uint8_t* char_data = font8x8[char_index];
    
    // 逆时针旋转90度绘制
    for (int orig_row = 0; orig_row < 8; orig_row++) {
        for (int orig_col = 0; orig_col < 8; orig_col++) {
            // 检查原始点阵中的像素
            if (char_data[orig_row] & (1 << (7 - orig_col))) {
                // 应用逆时针90度旋转：
                // 新的x坐标 = 原始行号
                // 新的y坐标 = 7 - 原始列号
                drawPixel(x + orig_row, y + (7 - orig_col), color);
            }
        }
    }
}
// 绘制文本

void drawText(int x, int y, const std::string& text, uint32_t color) {
    // 水平排列字符
    for (size_t i = 0; i < text.size(); i++) {
        drawChar(x + i * 9, y, text[i], color);
    }
}

// 清空帧缓冲区
void clearFramebuffer() {
    if (!use_framebuffer || !fb_info.fbp) return;
    memset(fb_info.fbp, 0, fb_info.screensize);
}

// 在屏幕上显示仪表盘 (精简横屏布局)
void displayDashboardOnScreen() {
    if (!use_framebuffer) return;
    
    // 获取实际屏幕尺寸
    int screen_width = fb_info.landscape ? fb_info.vinfo.xres : fb_info.vinfo.yres;
    int screen_height = fb_info.landscape ? fb_info.vinfo.yres : fb_info.vinfo.xres;
    
    // 部分清屏优化：只清除内容区域
    int clear_height = screen_height > 150 ? 150 : screen_height;
    for (int y = 0; y < clear_height; y++) {
        for (int x = 0; x < screen_width; x++) {
            drawPixel(x, y, 0x000000);
        }
    }
    
    // 复制信号数据
    std::map<std::string, SignalData> current_signals;
    {
        std::lock_guard<std::mutex> lock(g_data_mutex);
        current_signals = g_signal_values;
    }

    // 计算布局参数
    int col_width = screen_width / 3;
    int row_height = 30;
    int y_pos = 20;

    // 仪表盘标题 - 居中显示
    std::string title = "VEHICLE DASHBOARD";
    int title_x = (screen_width - title.length() * 9) / 2;
    drawText(title_x, y_pos, title, 0x00FF00);
    y_pos += 30;

    // 第一列: 车辆参数
    double speed = current_signals.count("SPEED") ? current_signals["SPEED"].value : 0;
    char speed_str[32];
    snprintf(speed_str, sizeof(speed_str), "SPEED: %3d km/h", static_cast<int>(speed));
    drawText(20, y_pos, speed_str, 0xFFFFFF);
    y_pos += row_height;
    
    double rpm = current_signals.count("RPM") ? current_signals["RPM"].value : 0;
    char rpm_str[32];
    snprintf(rpm_str, sizeof(rpm_str), "RPM: %6d", static_cast<int>(rpm));
    drawText(20, y_pos, rpm_str, 0xFFFFFF);
    y_pos += row_height;
    
    double fuel = current_signals.count("FUEL_LEVEL") ? current_signals["FUEL_LEVEL"].value : 0;
    char fuel_str[32];
    snprintf(fuel_str, sizeof(fuel_str), "FUEL: %3d%%", static_cast<int>(fuel));
    drawText(20, y_pos, fuel_str, 0xFFFFFF);
    
    // 第二列: 车辆状态
    y_pos = 50;
    int center_col = col_width;
    
    // 档位显示
    std::string gear = current_signals.count("GEAR_POS") ? 
                      gearToString(current_signals["GEAR_POS"].value) : "UNKNOWN";
    char gear_str[32];
    snprintf(gear_str, sizeof(gear_str), "GEAR: %-4s", gear.c_str());
    drawText(center_col, y_pos, gear_str, 0xFFFF00);
    y_pos += row_height;
    
    // 警告状态
    std::string warnText = "WARN: ";
    bool hasWarnings = false;
    for (const auto& name : {"CHECK_ENGINE", "OIL_PRESSURE", "BATTERY"}) {
        if (current_signals.count(name) && current_signals[name].value > 0) {
            warnText += formatName(name) + " ";
            hasWarnings = true;
        }
    }
    drawText(center_col, y_pos, hasWarnings ? warnText : "WARN: NONE", 
             hasWarnings ? 0xFF0000 : 0x00FF00);
    y_pos += row_height;
    
    // 门状态
    std::string doorText = "DOORS: ";
    bool doorsOpen = false;
    for (const auto& name : {"DRIVER_DOOR", "PASSENGER_DOOR", "TRUNK"}) {
        if (current_signals.count(name) && current_signals[name].value > 0) {
            doorText += formatName(name) + " ";
            doorsOpen = true;
        }
    }
    drawText(center_col, y_pos, doorsOpen ? doorText : "DOORS: CLOSED", 
             doorsOpen ? 0xFFFF00 : 0x00FF00);
    
    // 第三列: 其他信息
    y_pos = 50;
    int right_col = 2 * col_width;
    
    // 时间戳
    auto now = std::chrono::system_clock::now();
    auto now_time = std::chrono::system_clock::to_time_t(now);
    char time_str[32];
    std::strftime(time_str, sizeof(time_str), "%H:%M:%S", std::localtime(&now_time));
    drawText(right_col, y_pos, time_str, 0x00FFFF);
    y_pos += row_height;
    
    // 添加帧率信息
    static auto last_time = std::chrono::steady_clock::now();
    auto current_time = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(current_time - last_time).count();
    last_time = current_time;
    
    char fps_str[32];
    snprintf(fps_str, sizeof(fps_str), "FPS: %.1f", 1000.0f / elapsed);
    drawText(right_col, y_pos, fps_str, 0xAAAAAA);
}

int main() {
    // 尝试初始化帧缓冲区
    if (init_framebuffer()) {
        use_framebuffer = true;
        std::cout << "Using framebuffer for display" << std::endl;
        
        // 简单的方向测试
        drawText(50, 50, "TOP LEFT", 0xFFFFFF);
        drawText(fb_info.vinfo.xres - 100, 50, "TOP RIGHT", 0xFFFFFF);
        drawText(50, fb_info.vinfo.yres - 50, "BOTTOM LEFT", 0xFFFFFF);
        drawText(fb_info.vinfo.xres - 150, fb_info.vinfo.yres - 50, "BOTTOM RIGHT", 0xFFFFFF);
        
        sleep(2); // 显示测试2秒
        clearFramebuffer();
    } else {
        std::cout << "Using console for display" << std::endl;
    }

    // 注册信号处理
    signal(SIGINT, sigHandler);
    signal(SIGTERM, sigHandler);
    signal(SIGTTOU, SIG_IGN);

    VehicleCommunicationAPI api("/can_shared_mem");
    api.loadMessageDefinitions("/app/dashboard/Dashboard.dbc");
    
    // 注册信号处理回调
    api.registerSignalHandler("SPEED", signalHandler);
    api.registerSignalHandler("RPM", signalHandler);
    api.registerSignalHandler("FUEL_LEVEL", signalHandler);
    api.registerSignalHandler("GEAR_POS", signalHandler);
    
    api.registerSignalHandler("CHECK_ENGINE", signalHandler);
    api.registerSignalHandler("OIL_PRESSURE", signalHandler);
    api.registerSignalHandler("BATTERY", signalHandler);
    
    api.registerSignalHandler("DRIVER_DOOR", signalHandler);
    api.registerSignalHandler("PASSENGER_DOOR", signalHandler);
    api.registerSignalHandler("TRUNK", signalHandler);
    
    api.registerErrorHandler(errorHandler);
    
    api.start();
    
    std::cout << "Dashboard System Started. Press Ctrl+C to exit." << std::endl;
    
    // 使用固定帧率
    auto last_frame = std::chrono::steady_clock::now();
    
    while (g_running) {
        displayDashboardOnScreen();
        
        // 精确帧率控制
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_frame).count();
        last_frame = now;
        
        int sleep_time = 250 - elapsed; // 目标4Hz
        if (sleep_time > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(sleep_time));
        }
    }
    
    // 清理资源
    api.stop();
    if (use_framebuffer) {
        close_framebuffer();
    }
    
    return 0;
}
