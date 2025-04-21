#include <linux/gpio/consumer.h>
#include <linux/platform_device.h>

#define DEVICE_NAME "ds18b20"

struct ds18b20_data {
    struct gpio_desc *gpio;
    struct mutex lock;
};

static int ds18b20_read(struct device *dev, int *temp)
{
    u8 buf[9];
   // 实现1-Wire协议
    // [复位][跳过ROM][温度转换]
    // [复位][跳过ROM][读取暂存器]
    // 数据校验和计算

    *temp = (buf[1] << 8) | buf[0];

    return 0;
}

static ssize_t temp_show(struct device *dev, struct device_attribute *attr, char buf[])
{
    int temp, ret;
    ret = ds18b20_read(dev, &temp);
    return sprintf(buf, "%d\n", temp*625/100);
}

static DEVICE_ATTR_RO(temp);


