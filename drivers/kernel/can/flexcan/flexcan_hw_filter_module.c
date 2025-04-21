/* FlexCAN 硬件过滤模块 */
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/can/dev.h>

static int filter_id = 0;
module_param(filter_id, int, 0644);
MODULE_PARM_DESC(filter_id, "Maibox ID for hardware filtering");


static u32 filter_mask = 0x7FF;
module_param(filter_mask, int, 0644);
MODULE_PARAM_DESC(filter_mask, "11-bit CAN ID mask");

static struct net_device *can_dev;
static int __init flexcan_filter_init(void)
{
    struct can_priv *priv;

    can_dev = dev_get_by_name(&init_net, "can0");

    if(!can_dev) return -ENODEV;

    priv = netdev_priv(can_dev);

    priv->write_reg(priv, CAN_MB_MASK(filter_id), filter_mask);

    printk(KERN_INFO "flexCAN HW filter enabled on mb%d\n", filter_mask);

    return 0;
}


static void __exit flexcan_filter_exit(void)
{
    if (can_dev)
        dev_put(can_dev);
}


module_init(flexcan_filter_init);
module_exit(flexcan_filter_exit);
MODULE_LICENSE("GPL");



