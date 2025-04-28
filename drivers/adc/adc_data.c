#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/hrtimer.h>
#include <linux/iio/iio.h>
#include <linux/w1.h>
#include <linux/can.h>
#include <linux/can/dev.h>

#define DRIVER_NAME "engine_simulator"
#define CAN_ID 0x123
#define SAMPLE_INTERVAL_MS 100

static char* can_frame = "can0";
module_param(can_ifname, charp, 0644);
MODULE_PARM_DESC(can_ifname, "CAN interface name");

static int adc_chan = 0;
module_param(adc_chan, int , 0644);
MODULE_PARM_DESC(adc_chan, "ADC channel number");


struct engine_data {
    struct hrtimer timer;
    struct iio_channel* adc;
    struct w1_slave *temp_slave;
    struct net_device *can_dev;
    struct can_frame frame;
};

struct engine_data *ed;
//定时器回调
static enum hrtimer_restart sample_timer_callback(struct hrtimer* timer)
{
    struct engine_data *ed = container_of(timer, struct engine_data, timer);
    int val,ret;
    int temp_raw;
    char tmp_buf[128];

    //read adc simulator
    ret = iio_read_channel_raw(ed->adc, &val);
    if (ret < 0)
    {
        printk(KERN_ERR "ADC read error: %d\n", ret);
        goto  restart;
    }

    //red temp
    // if (ed->temp_slave && w1_reset_select_slave(&ed->temp_slave->master->dev, ed->temp_slave))
    // {
    //     w1_write_8(&ed->temp_slave->master->dev, W1_READ_SCRATCHPAD);
    //     w1_read_block(&ed->temp_slave->master->dev, tmp_buf, 9);
    //     temp_raw = (tmp_buf[1] << 8 | tmp_buf[0]);
    // }
    // else
    // {
    //     temp_raw = -1;
    // }


    //can
    ed->frame.can_id = CAN_ID;
    ed->frame.can_dlc = 2;
    ed->frame.data[0] = (val * 6000 / 4095) >> 8;
    ed->frame.data[1] = (val * 6000 / 4095) & 0xFF;
    // ed->frame.data[2] = (temp_raw >> 11) & 0xFF;
    // ed->frame.data[3] = (temp_raw >> 3) & 0xFF;
    // ed->frame.data[4] = (temp_raw & 0x7) << 5;

    //send can
    if (ed->can_dev) 
    {
        struct sk_buff *skb = alloc_can_skb(ed->can_dev, &ed->frame);
        if (!skb) 
        {
            printk(KERN_ERR "CAN frame allocation failed\n");
            goto restart;
        }
        netif_rx(skb); //逆向注入协议栈
    }

    restart:
        hrtimer_forward_now(timer, ms_to_ktime(SAMPLE_INTERVAL_MS));
        return HRTIMER_RESTART;
}

static int __init engine_sim_init(void)
{
    int ret = 0;
    
    ed = kzalloc(sizeof(*ed), GFP_KERNEL);
    if (!ed)
        return -ENOMEM;

    ed->adc = iio_channel_get(NULL, "adc1");
    if (IS_ERR(ed->adc))
    {
        ret = PTR_ERR(ed->adc);
        goto err_free;
    }

    ed->can_dev = dev_get_by_name(&init_net, can_ifname);
    if (!ed -> can_dev)
    {
        ret = -ENODEV;
        goto err_adc;
    }

    // 初始化温度传感器
    /* 需要根据实际1-Wire设备地址配置 */
    // ed->temp_slave = w1_slave_search_family(w1_masters[0], 0x28);


    //注册定时器回调
    hrtimer_init(&ed->timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
    ed->timer.function = sample_timer_callback;
    hrtimer_start(&ed->timer, ms_to_ktime(SAMPLE_INTERVAL_MS), HRTIMER_MODE_REL);

    return 0;

err_adc:
    iio_channel_release(ed->adc);
err_free:
    kfree(ed);
    return ret
}

static void __exit engine_sim_exit(void)
{
    if (ed) {
        hrtimer_cancel(&ed->timer);
        if (ed->can_dev)
            dev_put(ed->can_dev);
        if (ed->adc)
            iio_channel_release(ed->adc);
        kfree(ed);
    }
}

module_init(engine_sim_init);
module_exit(engine_sim_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Your Name");
MODULE_DESCRIPTION("Engine Speed & Temperature Simulator");
