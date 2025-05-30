#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <time.h>
#include <stdint.h> // 添加uint64_t支持

// 定义驱动接口
#define GET_TX_BUF _IOR('F', 0, int)
#define GET_RX_BUF _IOR('F', 1, int)
#define GET_TX_DONE _IOR('F', 2, int)
#define GET_RX_DONE _IOR('F', 3, int)

// #define SET_LOOPBACK _IOW('F', 2, int) // 如果需要，在驱动中实现
#define CAN_FRAME_SIZE sizeof(struct can_frame)  // 根据实际结构体大小调整

// CAN帧结构体定义（应与内核一致）
struct can_frame {
    unsigned int can_id;
    unsigned char can_dlc;
    unsigned char data[8];
};

// 时间戳宏
#define TIMESTAMP_SIZE sizeof(uint64_t)

int main(int argc, char *argv[]) {

    int dev_fd = open("/dev/flexcan_dma", O_RDWR);
    int rx_fd, index,i;
    
    while (1) {
        // 获取接收缓冲区
        ioctl(dev_fd, GET_RX_BUF, &rx_fd);
        
        // 映射DMA缓冲区
        struct can_frame *frame = mmap(NULL, CAN_FRAME_SIZE, 
                                     PROT_READ, MAP_SHARED, rx_fd, 0);
        
        // 处理数据
        printf("Received CAN ID: 0x%X\n", frame->can_id);
        for (i = 0; i < frame->can_dlc; i++) {
            printf("%02X ", frame->data[i]);
        }
        printf("\n");
        
        // 释放资源
        munmap(frame, CAN_FRAME_SIZE);
        
        // 通知内核缓冲区释放
        ioctl(dev_fd, GET_RX_DONE, &index);

        close(rx_fd);

    }
    
    close(dev_fd);
    return 0;

}