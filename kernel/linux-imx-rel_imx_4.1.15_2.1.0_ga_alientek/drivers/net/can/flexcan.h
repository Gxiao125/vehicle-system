#ifndef FLEXCAN_H
#define FLEXCAN_H

#include <linux/netdevice.h>
#include "can_common.h"
#include <linux/dmaengine.h>
#include <linux/dma-mapping.h>
extern int register_flexcan_transport(struct net_device *dev, transport_rx_callback_t callback, struct module *owner);

extern void unregister_flexcan_transport(struct net_device *dev);

extern int flexcan_transport_send(struct net_device *dev, struct can_frame *cf);

extern int flexcan_dma_send(struct net_device *dev, dma_addr_t dma_addr);
#endif