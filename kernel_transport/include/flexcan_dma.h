#ifndef FLEXCAN_DMA_H
#define FLEXCAN_DMA_H

#include <linux/types.h>
#include <linux/can.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/dma-buf.h>
#include <linux/slab.h>
#include <linux/wait.h>
#include <linux/poll.h>
#include <linux/atomic.h>
#include <linux/sched.h>
#include <linux/uaccess.h>
#include <linux/can.h>
#include <linux/io.h>
#include <linux/circ_buf.h>
#include <linux/workqueue.h>
#include <linux/netdevice.h>
#include <linux/cdev.h>
#include <linux/device.h>
struct flexcan_dma_ops {
    struct can_frame* (*get_frame)(int *index);
    void (*rx_data_complete)(int index);
};

extern int flexcan_dma_register_ops(const struct flexcan_dma_ops *ops);
extern void flexcan_dma_unregister_ops(void);
extern int flexcan_hw_xmit(struct net_device *dev, dma_addr_t dma_addr, void *vaddr);


#endif
