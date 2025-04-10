#!/bin/sh
make ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- distclean
make ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- imx_alientek_emmc_defconfig
make ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- menuconfig
make ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- all -j12

# 检查编译是否成功
if [ $? -ne 0 ]; then
    echo "编译失败，请检查错误！"
    exit 1
fi

# 创建目标文件夹（如果不存在）
mkdir -p value

# 复制内核镜像和设备树文件
echo "正在复制编译产物到 value 文件夹..."
cp arch/arm/boot/zImage  ../../value/ 2>/dev/null || echo "错误:zImage 文件未找到！"
cp arch/arm/boot/dts/imx6ull-alientek-emmc.dtb  ../../value/ 2>/dev/null || echo "错误：.dtb 文件未找到！"

echo "操作完成！请在 value 文件夹中检查文件。"
