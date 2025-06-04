#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <string.h>
#include <time.h>
#include <linux/can.h>
#include <errno.h>

#define GET_TX_BUF_INDEX       _IOR('F', 0, int)
#define GET_ALL_TX_BUFS        _IOR('F', 2, int)
#define DMA_POOL_SIZE 64

int main(int argc, char *argv[]) {
    int cdev_fd, index;
    int tx_fds[DMA_POOL_SIZE];
    struct can_frame *frame;
    int num_frames = 100;
    int i, j, ret;
    struct timespec ts;
    int tx_delay = 1000; // 默认1ms延迟
    
    if (argc > 1) num_frames = atoi(argv[1]);
    if (argc > 2) tx_delay = atoi(argv[2]);

    // 1. 打开字符设备
    if ((cdev_fd = open("/dev/flexcan_dma", O_RDWR)) < 0) {
        perror("open failed");
        return -1;
    }

    // 2. 获取所有TX缓冲区的fd
    if (ioctl(cdev_fd, GET_ALL_TX_BUFS, tx_fds) < 0) {
        perror("GET_ALL_TX_BUFS failed");
        close(cdev_fd);
        return -1;
    }

    // 3. 映射所有TX缓冲区
    struct can_frame *frames[DMA_POOL_SIZE] = {NULL};
    
    for (i = 0; i < DMA_POOL_SIZE; i++) {
        frames[i] = mmap(NULL, sizeof(struct can_frame), 
                        PROT_READ | PROT_WRITE, 
                        MAP_SHARED, tx_fds[i], 0);
        
        if (frames[i] == MAP_FAILED) {
            fprintf(stderr, "mmap buffer %d failed: %s\n", i, strerror(errno));
            for (j = 0; j < i; j++) {
                munmap(frames[j], sizeof(struct can_frame));
                close(tx_fds[j]);
            }
            close(cdev_fd);
            return -1;
        }
    }

    // 4. 发送CAN帧（带流量控制）
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);
    int sent_count = 0;
    
    for (i = 0; i < num_frames; i++) {
        // 获取缓冲区索引（带错误重试）
        int retries = 0;
        while (1) {
            ret = ioctl(cdev_fd, GET_TX_BUF_INDEX, &index);
            if (ret == 0 && index >= 0 && index < DMA_POOL_SIZE) break;
            
            if (retries++ > 10) {
                fprintf(stderr, "Failed to get buffer after %d retries\n", retries);
                break;
            }
            usleep(500); // 500us重试延迟
        }
        
        if (index < 0 || index >= DMA_POOL_SIZE) {
            fprintf(stderr, "Invalid index %d at frame %d\n", index, i);
            continue;
        }
        
        frame = frames[index];
        
        frame->can_id = 0x123;
        frame->can_dlc = 8;
        for (j = 0; j < 8; j++) {
            frame->data[j] = 'A';
        }
        
        // 确保数据同步到设备
        // __builtin_ia32_mfence(); // 内存屏障
        msync(frame, sizeof(struct can_frame), MS_SYNC);
        
        // 触发传输
        if (write(cdev_fd, &index, sizeof(index)) != sizeof(index)) {
            fprintf(stderr, "Write failed for frame %d: %s\n", i, strerror(errno));
            continue;
        }
        
        sent_count++;
        
        // 添加可调延迟
        if (tx_delay > 0) {
            ts.tv_sec = tx_delay / 1000000;
            ts.tv_nsec = (tx_delay % 1000000) * 1000;
            nanosleep(&ts, NULL);
        }
        
        // 每10帧打印进度
        if (i % 10 == 0) {
            printf("Sent frame %d/%d (ID:0x%X)\n", i+1, num_frames, frame->can_id);
        }
    }
    
    // 等待所有帧发送完成
    usleep(50000); // 50ms
    
    clock_gettime(CLOCK_MONOTONIC, &end);
    double elapsed = (end.tv_sec - start.tv_sec) + 
                    (end.tv_nsec - start.tv_nsec) / 1e9;
    
    printf("Sent %d/%d frames in %.3f seconds (%.1f fps)\n", 
           sent_count, num_frames, elapsed, sent_count / elapsed);

    // 5. 清理
    for (i = 0; i < DMA_POOL_SIZE; i++) {
        munmap(frames[i], sizeof(struct can_frame));
        close(tx_fds[i]);
    }
    close(cdev_fd);
    
    return 0;
}