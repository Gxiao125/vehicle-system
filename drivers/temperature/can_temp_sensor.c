#include <linux/module.h>
#include <linux/can/dev.h>

static struct can_device *can_dev;

//CAN rx

static void can_rx_handler(struct can_frame *frame, void *priv)
{
    if (frame->can_id == 0x123)
    {
        int temp = frame->data[0];
        printk("Temperature: %d C\n", temp);
    }
}

static int __init can_temp_init(void) 
{
    //get can
    can_dev = can_get_dev("can0");
    if (!can_dev)
        return -ENODEV;

    // register
    can_rx_register(can_dev, can_rx_handler, NULL);
    
    return 0;
}

static void __exit can_temp_exit(void)
{
    can_rx_unregister(can_dev, can_rx_handler);
    can_put_dev(can_dev);
}

module_init(can_temp_init);
module_exit(can_temp_exit);
MODULE_LICENSE("GPL");

