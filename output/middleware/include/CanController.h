#pragma once
#include <vector>
#include <functional>
#include <thread>
#include <atomic>
#include <mutex>
#include <queue>
#include <linux/can.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <poll.h>
#include <condition_variable>
#include <string>

#define DMA_POOL_SIZE 64

struct EnhancedCANFrame : can_frame {
    const can_frame* frame_ptr;
    uint32_t timestamp;
};

using FrameHandler = std::function<void(const EnhancedCANFrame&)>;

class FlexCANController {
    public:
        FlexCANController(const std::string& device_path);

        ~FlexCANController();

        bool init();
        void start();
        void stop();
        bool sendFrame(const can_frame& frame);
        void registerFrameHandler(FrameHandler handler);
        void setTxDelay(uint32_t delay_us) {tx_delay_us_ = delay_us;}

        std::string getDevicePath() {
            return device_path_;
        }

    private:
        void receiveThreadFunc();
        void txBufferManager();

        bool mapBuffers();
        void unmapBuffers();

        enum IOCtlCmds {
            GET_ALL_RX_BUFS  = _IOR('F', 4, int[DMA_POOL_SIZE]),
            GET_READY_INDEX  = _IOR('F', 5, int),
            RELEASE_BUF      = _IOR('F', 6, int),
            GET_READY_COUNT  = _IOR('F', 7, int),
            GET_TX_BUF_INDEX = _IOR('F', 0, int),
            GET_ALL_TX_BUFS   = _IOR('F', 2, int),
            INIT_DMA_POOL   =  _IOR('F' , 8, size_t)
        };

        std::string device_path_;
        int dev_fd_{-1};

        std::atomic<bool> running_{false};
        std::thread receive_thread_;
        std::vector<void*> rx_buffers_;
        std::vector<FrameHandler> frame_handlers_;
        std::mutex handler_mutex_;


        std::vector<int> tx_fds_;
        std::vector<can_frame*> tx_frames_;
        std::queue<can_frame> tx_queue_;
        std::mutex tx_mutex_;
        std::condition_variable tx_cv_;
        std::thread tx_thread_;
        uint32_t tx_delay_us_{1000};
};

