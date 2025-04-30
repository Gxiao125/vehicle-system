#include <linux/module.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/netdevice.h>
#include <linux/can.h>
#include <linux/can/dev.h>
#include <linux/timer.h>
#include <linux/workqueue.h>  
#include <linux/can/raw.h>
#define CAN_ID 0x123
#define ADC_RAW_PATH "/sys/bus/iio/devices/iio:device0/in_voltage1_raw"

static int read_adc_value(int *val);
static void send_can_frame(int speed);
static struct timer_list adc_timer;
static struct net_device *can_dev;
static struct file *adc_filp;
static struct work_struct adc_work;  // 定义工作队列

void send_can_frame(int speed)  
{
    struct sk_buff *skb;
    struct can_frame *frame;

    if (!can_dev || !netif_running(can_dev))
        return;

    skb = alloc_can_skb(can_dev, &frame);
    if (!skb) {
        printk(KERN_ERR "CAN skb alloc failed\n");
        return;
    }

    frame->can_id = CAN_ID;
    frame->can_dlc = 2;
    frame->data[0] = speed >> 8;
    frame->data[1] = speed & 0xFF;

    netif_rx(skb);
    printk(KERN_DEBUG "CAN frame sent\n");
}
//--------------------------------------------------
// 工作队列处理函数（进程上下文）
//--------------------------------------------------
static void adc_work_handler(struct work_struct *work)
{

    int adc_val, speed;
    
    if (read_adc_value(&adc_val) == 0) {
        speed = (adc_val * 200) / 4095;
        send_can_frame(speed);
    }

}

//--------------------------------------------------
// 定时器回调函数（中断上下文）
//--------------------------------------------------
static void adc_timer_callback(unsigned long data)
{
    // 仅调度工作队列，不直接操作文件或CAN
    schedule_work(&adc_work);
    mod_timer(&adc_timer, jiffies + msecs_to_jiffies(100));
}

//--------------------------------------------------
// ADC读取函数（需在进程上下文调用）
//--------------------------------------------------
int read_adc_value(int *val)
{
    char buf[16] = {0};
    loff_t pos = 0;
    int ret;

    if (IS_ERR(adc_filp))
    {
        printk("err adc_filp");
        return PTR_ERR(adc_filp);
    }

    ret = kernel_read(adc_filp, pos, buf, sizeof(buf)-1);
    if (ret < 0) {
        printk(KERN_ERR "Read ADC failed: %d\n", ret);
        return ret;
    }

    if (ret >= 0 && ret < sizeof(buf))
        buf[ret] = '\0';
    else
        buf[sizeof(buf)-1] = '\0';

    if (kstrtoint(buf, 10, val)) {
        printk(KERN_ERR "Convert ADC value failed\n");
        return -EINVAL;
    }

    return 0;
}

//--------------------------------------------------
// 模块初始化
//--------------------------------------------------
static int __init gpio_adc_init(void)
{
    printk(KERN_INFO "Opening ADC path: %s\n", ADC_RAW_PATH);

    // 初始化工作队列
    INIT_WORK(&adc_work, adc_work_handler);

    // 打开ADC文件（在进程上下文中提前打开）
    adc_filp = filp_open(ADC_RAW_PATH, O_RDONLY, 0);
    if (IS_ERR(adc_filp)) {
        printk(KERN_ERR "Open ADC failed: %ld, Path: %s\n", PTR_ERR(adc_filp), ADC_RAW_PATH);
        return PTR_ERR(adc_filp);
    }

    // 获取CAN设备
    can_dev = dev_get_by_name(&init_net, "can0");
    if (!can_dev) {
        filp_close(adc_filp, NULL);
        printk(KERN_ERR "CAN device can0 not found\n");
        return -ENODEV;
    }

    // 检查并启动CAN接口
    if (!netif_running(can_dev)) {
        int err = dev_open(can_dev);
        if (err) {
            printk(KERN_ERR "Failed to start can0: %d\n", err);
            dev_put(can_dev);
            filp_close(adc_filp, NULL);
            return err;
        }
    }

    // 启动定时器（回调函数仅触发工作队列）
printk("start timer\n");
    init_timer(&adc_timer);
    adc_timer.function = adc_timer_callback;
    adc_timer.expires = jiffies + msecs_to_jiffies(100);
    add_timer(&adc_timer);

printk("init success\n");

    return 0;
}

//--------------------------------------------------
// 模块退出
//--------------------------------------------------
static void __exit gpio_adc_exit(void)
{
    del_timer_sync(&adc_timer);
    cancel_work_sync(&adc_work); // 等待工作队列完成
    if (!IS_ERR(adc_filp))
        filp_close(adc_filp, NULL);
    if (can_dev)
        dev_put(can_dev);
}

module_init(gpio_adc_init);
module_exit(gpio_adc_exit);
MODULE_LICENSE("GPL");

