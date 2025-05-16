#include <linux/module.h>
#include <linux/netdevice.h> 
#include "../include/can_common.h"
#include "../include/flexcan.h"
#include "transport.h"

static void transport_rx_handler(struct can_frame *frame)
{
    printk("Transport Layer Received: ID=0x%X, DLC=%d\n", frame->can_id, frame->can_dlc);
}

void transport_send_frame(struct can_frame *frame){
    struct net_device *dev = dev_get_by_name(&init_net, "can0");
    if (!dev)
        return;

    int ret = flexcan_transport_send(dev,frame);
    if(ret)
        pr_err("Send failed: %d\n", ret);
    
    dev_put(dev);
}

static int __init transport_init(void)
{
    struct net_device *dev = dev_get_by_name(&init_net, "can0");
    if (!dev) return -ENODEV;

    int ret = register_flexcan_transport(dev, transport_rx_handler, THIS_MODULE);
    dev_put(dev);
    return ret;
}

static void __exit transport_exit(void)
{
    struct net_device *dev = dev_get_by_name(&init_net, "can0");
    if (!dev) return;

    unregister_flexcan_transport(dev);
    dev_put(dev);

    return;
}

module_init(transport_init);
module_exit(transport_exit);
MODULE_SOFTDEP("pre: flexcan");
MODULE_LICENSE("GPL");

