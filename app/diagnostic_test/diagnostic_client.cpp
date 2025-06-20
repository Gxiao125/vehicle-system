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

class DiagnosticTool {
public:
    DiagnosticTool(const std::string& shm_name) 
        : api_(shm_name), 
          running_(false),
          response_received_(false) {
        
        // 注册响应处理
        api_.registerRawDataHandler([this](uint32_t can_id, const std::vector<uint8_t>& data) {
            if (can_id == 0x711) { // DiagnosticResponse
                handleInitialResponse(data);
            }
            else if (can_id == 0x712) { // DiagnosticData
                handleDataBlock(data);
            }
        });
    }

    void start() {
        api_.loadMessageDefinitions("diagnostic.dbc");
        api_.start();
        running_ = true;
    }

    void stop() {
        running_ = false;
        api_.stop();
    }

    void runDiagnosticTest() {
        // 准备诊断请求数据
        std::vector<uint8_t> request;
        request.push_back(0x85); // 扩展诊断会话
        
        // 添加更多请求参数
        for (int i = 0; i < 15; i++) {
            request.push_back(static_cast<uint8_t>(i));
        }

        // 发送诊断请求
        {
            std::unique_lock lock(mutex_);
            response_received_ = false;
            received_data_.clear();
            expected_blocks_ = 0;
            current_block_ = 0;
        }
        
        api_.sendRawMessage(0x710, request);
        std::cout << "Sent diagnostic request. Size: " << request.size() << " bytes" << std::endl;

        // 等待响应
        std::unique_lock lock(mutex_);
        if (cv_response_.wait_for(lock, 5s, [this] { 
            return response_received_ && (received_data_.size() >= expected_data_size_); 
        })) {
            std::cout << "Diagnostic test completed successfully!" << std::endl;
            std::cout << "Received data size: " << received_data_.size() << " bytes" << std::endl;
            
            // 验证数据完整性
            bool data_valid = true;
            for (size_t i = 0; i < received_data_.size(); i++) {
                if (received_data_[i] != static_cast<uint8_t>(i & 0xFF)) {
                    data_valid = false;
                    break;
                }
            }
            
            if (data_valid) {
                std::cout << "Data integrity: VALID" << std::endl;
            } else {
                std::cout << "Data integrity: INVALID" << std::endl;
            }
        } else {
            std::cerr << "Diagnostic test timed out" << std::endl;
            std::cerr << "Received " << received_data_.size() << " of " 
                      << expected_data_size_ << " bytes" << std::endl;
        }
    }

private:
    void handleInitialResponse(const std::vector<uint8_t>& data) {
        if (data.empty()) {
            std::cerr << "Empty diagnostic response" << std::endl;
            return;
        }

        uint8_t response_code = data[0];
        
        if (response_code != 0x00) {
            std::cerr << "Diagnostic error: 0x" << std::hex 
                      << static_cast<int>(response_code) << std::dec << std::endl;
            return;
        }

        // 在实际系统中，这里会解析响应以确定数据大小
        // 为简化，我们固定期望256字节
        {
            std::unique_lock lock(mutex_);
            expected_data_size_ = 256;
            response_received_ = true;
        }
        
        cv_response_.notify_one();
        std::cout << "Received positive diagnostic response" << std::endl;
    }

    void handleDataBlock(const std::vector<uint8_t>& data) {
        if (data.empty()) {
            std::cerr << "Empty data block" << std::endl;
            return;
        }

        uint8_t block_counter = data[0];
        std::vector<uint8_t> block_data(data.begin() + 1, data.end());
        
        {
            std::unique_lock lock(mutex_);
            
            // 检查数据块顺序
            if (block_counter != current_block_) {
                std::cerr << "Block sequence error! Expected " 
                          << static_cast<int>(current_block_)
                          << ", got " << static_cast<int>(block_counter)
                          << std::endl;
                return;
            }
            
            // 添加到接收数据
            received_data_.insert(received_data_.end(), 
                                block_data.begin(), 
                                block_data.end());
            
            current_block_++;
            
            std::cout << "Received data block " << static_cast<int>(block_counter)
                      << ", size: " << block_data.size() << " bytes"
                      << ", total: " << received_data_.size() << " bytes" << std::endl;
        }
        
        cv_response_.notify_one();
    }

    VehicleCommunicationAPI api_;
    std::atomic<bool> running_;
    
    // 响应接收同步
    std::mutex mutex_;
    std::condition_variable cv_response_;
    bool response_received_;
    size_t expected_data_size_ = 0;
    std::vector<uint8_t> received_data_;
    uint8_t current_block_ = 0;
};

int main() {
    DiagnosticTool tool("veh_comm_shm");
    tool.start();
    
    std::cout << "Running diagnostic test..." << std::endl;
    tool.runDiagnosticTest();
    
    tool.stop();
    return 0;
}


