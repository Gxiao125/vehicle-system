#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <poll.h>
#include <linux/can.h>

#define DMA_POOL_SIZE 64
#define GET_ALL_RX_BUFS  _IOR('F', 4, int[DMA_POOL_SIZE])
#define GET_READY_INDEX  _IOR('F', 5, int)
#define RELEASE_BUF      _IOR('F', 6, int)
#define GET_READY_COUNT  _IOR('F', 7, int)

// 处理CAN帧的函数
void process_can_frame(struct can_frame *frame) {
    printf("Received CAN frame: ID=0x%X, DLC=%d, Data=", 
           frame->can_id & CAN_EFF_MASK, 
           frame->can_dlc);
    
    int i;
    for (i = 0; i < frame->can_dlc; i++) {
        printf("%02X ", frame->data[i]);
    }
    printf("\n");
}

int main() {

    int dev_fd = open("/dev/flexcan_dma", O_RDWR);
    if (dev_fd < 0) {
        perror("打开设备失败");
        return 1;
    }

    // 1. 获取所有RX缓冲区的fd
    int rx_fds[DMA_POOL_SIZE];
    if (ioctl(dev_fd, GET_ALL_RX_BUFS, rx_fds) < 0) {
        perror("获取RX缓冲区失败");
        close(dev_fd);
        return 1;
    }

    // 2. 预映射所有缓冲区
    void *rx_buffers[DMA_POOL_SIZE];
    int i;
    for (i = 0; i < DMA_POOL_SIZE; i++) {
        rx_buffers[i] = mmap(NULL, sizeof(struct can_frame), PROT_READ, 
                            MAP_SHARED, rx_fds[i], 0);
        close(rx_fds[i]); // 映射后即可关闭fd
        
        if (rx_buffers[i] == MAP_FAILED) {
            perror("mmap失败");
            // 清理已映射的缓冲区
            int j;
            for (j = 0; j < i; j++) munmap(rx_buffers[j], sizeof(struct can_frame));
            close(dev_fd);
            return 1;
        }
    }

    struct pollfd fds = {
        .fd = dev_fd,
        .events = POLLIN
    };

    while (1) {
        // 3. 使用poll等待数据
        int ret = poll(&fds, 1, -1);
        if (ret < 0) {
            perror("poll错误");
            break;
        }

        if (fds.revents & POLLIN) {
            // 4. 获取就绪缓冲区数量
            int ready_count;
            if (ioctl(dev_fd, GET_READY_COUNT, &ready_count) < 0) {
                perror("获取就绪数量失败");
                continue;
            }
            
            if (ready_count > 0) {
                    printf("有 %d 个就绪帧\n", ready_count);
                int i;
                // 5. 批量处理所有就绪缓冲区
                for (i = 0; i < ready_count; i++) {
                    int idx;
                    if (ioctl(dev_fd, GET_READY_INDEX, &idx) < 0) {
                        perror("获取就绪索引失败");
                        continue;
                    }
                    
                    // 6. 直接访问预映射的内存
                    struct can_frame *frame = (struct can_frame *)rx_buffers[idx];
                    process_can_frame(frame);
                    
                    // 7. 释放缓冲区
                    if (ioctl(dev_fd, RELEASE_BUF, &idx) < 0) {
                        perror("释放缓冲区失败");
                        printf("失败索引: %d\n", idx);
                    } else {
                        printf("成功释放缓冲区: %d\n", idx);
                    }
                }
            }
        }
    }

    // 清理
    for (i = 0; i < DMA_POOL_SIZE; i++) {
        munmap(rx_buffers[i], sizeof(struct can_frame));
    }
    close(dev_fd);
    return 0;

}
