#include "../include/CanController.h"
#include <chrono>
#include <system_error>
#include <string.h>
#include <iostream>
FlexCANController::FlexCANController(const std::string& device_path): 
                    device_path_(device_path) ,rx_buffers_(DMA_POOL_SIZE, nullptr) {
    
}


FlexCANController::~FlexCANController() {
    stop();
    unmapBuffers();
    if (dev_fd_ >= 0) close(dev_fd_);
}


bool FlexCANController::init() {
    dev_fd_ = open(device_path_.c_str(), O_RDWR);
    if (dev_fd_ < 0){
        perror("failed to open device");
        return false;
    }

    return mapBuffers();
}

bool FlexCANController::mapBuffers() {
    int rx_fds[DMA_POOL_SIZE];
    if (ioctl(dev_fd_, GET_ALL_RX_BUFS, rx_fds) < 0) {
        std::cerr << "Failed to get RX buffers: " << strerror(errno) << std::endl;
        return false;
    }

    for (int i = 0; i < DMA_POOL_SIZE; i++) {
        rx_buffers_[i] = mmap(nullptr, sizeof(can_frame), PROT_READ, 
                             MAP_SHARED, dev_fd_, 0);
        if (rx_buffers_[i] == MAP_FAILED) {
            std::cerr << "mmap RX buffer failed: " << strerror(errno) << std::endl;
            return false;
        }
    }

    tx_fds_.resize(DMA_POOL_SIZE);
    tx_frames_.resize(DMA_POOL_SIZE, nullptr);
    
    if (ioctl(dev_fd_, GET_ALL_TX_BUFS, tx_fds_.data()) < 0) {
        std::cerr << "Failed to get TX buffers: " << strerror(errno) << std::endl;
        return false;
    }

    for (int i = 0; i < DMA_POOL_SIZE; i++) {
        tx_frames_[i] = static_cast<can_frame*>(
            mmap(nullptr, sizeof(can_frame), 
                 PROT_READ | PROT_WRITE, 
                 MAP_SHARED, tx_fds_[i], 0));
        
        if (tx_frames_[i] == MAP_FAILED) {
            std::cerr << "mmap TX buffer failed: " << strerror(errno) << std::endl;
            return false;
        }
    }
    return true;
}

void FlexCANController::unmapBuffers() {
    for (auto& buf : rx_buffers_) {
        if (buf) {
            munmap(buf, sizeof(can_frame));
            buf = nullptr;
        }
    }
    
    for (auto& frame : tx_frames_) {
        if (frame) {
            munmap(frame, sizeof(can_frame));
            frame = nullptr;
        }
    }
}

void FlexCANController::start() {
    if (running_) return;

    running_ = true;
    receive_thread_ = std::thread(&FlexCANController::receiveThreadFunc, this);
    tx_thread_ = std::thread(&FlexCANController::txBufferManager, this);
}

void FlexCANController::stop() {
    running_ = false;
    tx_cv_.notify_all();

    if (receive_thread_.joinable()) receive_thread_.join();
    if (tx_thread_.joinable()) tx_thread_.join();
}

void FlexCANController::receiveThreadFunc() {
    struct pollfd fds = {.fd = dev_fd_, .events = POLLIN};

    while(running_) {
        int ret = poll(&fds, 1, 100);
        if (!running_) break;

        if (ret < 0) {
            if (errno == EINTR) continue;
            perror("poll error");
            break;
        }

        if (fds.revents & POLLIN) {
            int ready_count;
            if (ioctl(dev_fd_, GET_READY_COUNT, &ready_count) < 0) {
                perror("get ready count failed");
                continue;
            }

            for (int i = 0; i < ready_count; i++) {
                int idx;
                if (ioctl(dev_fd_, GET_READY_INDEX, &idx) < 0) {
                    perror("get ready index failed");
                    continue;
                }
                if (idx < 0 || idx >= DMA_POOL_SIZE) continue;

                // 直接使用映射的内存区域，避免拷贝
                const can_frame* frame = static_cast<const can_frame*>(rx_buffers_[idx]);
                
                EnhancedCANFrame enhanced_frame;
                enhanced_frame.frame_ptr = frame;  // 直接传递指针
                enhanced_frame.timestamp = static_cast<uint32_t>(
                    std::chrono::duration_cast<std::chrono::microseconds>(
                        std::chrono::system_clock::now().time_since_epoch()).count());

                {
                    std::lock_guard<std::mutex> lock(handler_mutex_);
                    for (auto& handler : frame_handlers_) {
                        handler(enhanced_frame); 
                    }
                }

                if (ioctl(dev_fd_, RELEASE_BUF, &idx) < 0) {
                    perror("release buf failed");
                }
            }
        }
    }
}


void FlexCANController::txBufferManager() {
    while (running_) {
        std::unique_lock<std::mutex> lock(tx_mutex_);
        
        // 等待队列中有帧可发送或停止信号
        tx_cv_.wait(lock, [this]{
            return !tx_queue_.empty() || !running_;
        });

        if (!running_) break;

        // 批量处理最多10帧
        int frames_to_send = std::min(static_cast<int>(tx_queue_.size()), 10);
        
        for (int i = 0; i < frames_to_send; i++) {
            int index;
            if (ioctl(dev_fd_, GET_TX_BUF_INDEX, &index) < 0 || 
                index < 0 || index >= static_cast<int>(tx_frames_.size())) {
                std::this_thread::sleep_for(std::chrono::microseconds(500));
                continue;
            }

            // 直接修改映射的TX缓冲区
            *tx_frames_[index] = tx_queue_.front(); 
            tx_queue_.pop();
            
            // 内存屏障确保数据一致性
            __sync_synchronize();

            // 通知内核发送
            if (write(dev_fd_, &index, sizeof(index)) != sizeof(index)) {
                std::cerr << "TX write failed: " << strerror(errno) << std::endl;
                tx_queue_.push(*tx_frames_[index]); // 重新放回队列
                break;
            }

            // 应用发送延迟
            if (tx_delay_us_ > 0) {
                std::this_thread::sleep_for(std::chrono::microseconds(tx_delay_us_));
            }
        }
        
        // 短暂休眠避免忙等待
        std::this_thread::sleep_for(std::chrono::microseconds(50));
    }
}

bool FlexCANController::sendFrame(const can_frame& frame) {
    std::lock_guard<std::mutex> lock(tx_mutex_);
    if (frame.can_dlc > CAN_MAX_DLEN) {
        std::cerr << "Invalid frame length: " << frame.can_dlc << std::endl;
        return false;
    }
    
    if (tx_queue_.size() > 100) {
        std::cerr << "TX queue full" << std::endl;
        return false;
    }
    
    tx_queue_.push(frame);
    tx_cv_.notify_one();
    return true;
}


void FlexCANController::registerFrameHandler(FrameHandler handler) {
    std::lock_guard<std::mutex> lock(handler_mutex_);
    frame_handlers_.push_back(handler);
}
