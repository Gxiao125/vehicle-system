#include "../include/VehicleCommunicationAPI.h"
#include <iostream>
#include <vector>
#include <thread>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <chrono>
#include <cstring>

using namespace std::chrono_literals;

class BodyDiagnosticServer {
public:
    BodyDiagnosticServer(const std::string& shm_name) 
        : api_(shm_name), 
          running_(false) {
        
        // 注册诊断请求处理
        api_.registerRawDataHandler([this](uint32_t can_id, const std::vector<uint8_t>& data) {
            if (can_id == 0x710) { // DiagnosticRequest
                handleDiagnosticRequest(data);
            }
        });
    }

    void start() {
        api_.loadMessageDefinitions("diagnostic.dbc");
        api_.start();
        running_ = true;
        server_thread_ = std::thread(&BodyDiagnosticServer::run, this);
    }

    void stop() {
        running_ = false;
        if (server_thread_.joinable()) server_thread_.join();
        api_.stop();
    }

private:
    void run() {
        while (running_) {
            // 模拟车身数据采集
            std::this_thread::sleep_for(100ms);
        }
    }

    void handleDiagnosticRequest(const std::vector<uint8_t>& data) {
        if (data.size() < 1) {
            std::cerr << "Invalid diagnostic request" << std::endl;
            return;
        }

        uint8_t session_type = data[0];
        std::vector<uint8_t> request_data(data.begin() + 1, data.end());

        std::cout << "Received diagnostic request. Session: 0x" 
                  << std::hex << static_cast<int>(session_type) << std::dec
                  << ", Data size: " << request_data.size() << " bytes" << std::endl;

        // 处理请求并准备响应
        std::vector<uint8_t> response = processDiagnosticRequest(session_type, request_data);

        // 发送初始响应
        std::vector<uint8_t> initial_response;
        initial_response.push_back(0x00); // Success response code
        api_.sendRawMessage(0x711, initial_response);

        // 分块发送大数据响应
        constexpr size_t BLOCK_SIZE = 7; // 每块7字节数据（56位）
        uint8_t block_counter = 0;
        
        for (size_t offset = 0; offset < response.size(); offset += BLOCK_SIZE) {
            size_t bytes_to_send = std::min(BLOCK_SIZE, response.size() - offset);
            
            std::vector<uint8_t> block_data;
            block_data.push_back(block_counter++);
            block_data.insert(block_data.end(), 
                             response.begin() + offset, 
                             response.begin() + offset + bytes_to_send);
            
            api_.sendRawMessage(0x712, block_data);
            std::this_thread::sleep_for(10ms); // 防止总线过载
        }
    }

    std::vector<uint8_t> processDiagnosticRequest(uint8_t session_type, 
                                                const std::vector<uint8_t>& request) {
        // 在实际应用中，这里会有真实的诊断逻辑
        // 现在我们返回一个模拟的大数据响应
        
        const size_t response_size = 256; // 256字节响应数据
        std::vector<uint8_t> response;
        response.reserve(response_size);
        
        // 生成模式数据: 0x00, 0x01, ... 0xFF, 0x00, ...
        for (size_t i = 0; i < response_size; i++) {
            response.push_back(static_cast<uint8_t>(i & 0xFF));
        }
        
        return response;
    }

    VehicleCommunicationAPI api_;
    std::thread server_thread_;
    std::atomic<bool> running_;
};

int main() {
    BodyDiagnosticServer server("veh_comm_shm");
    std::cout << "Body Diagnostic Server starting..." << std::endl;
    server.start();
    
    // 运行30秒后退出
    std::this_thread::sleep_for(30s);
    
    server.stop();
    std::cout << "Body Diagnostic Server stopped." << std::endl;
    return 0;
}
