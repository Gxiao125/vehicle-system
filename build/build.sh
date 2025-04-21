#!/bin/bash
set -e

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" &> /dev/null && pwd)
PROJECT_ROOT="$SCRIPT_DIR/.."
OUTPUT_DIR="$PROJECT_ROOT/output"

# 工具链路径
TOOLCHAIN="$PROJECT_ROOT/build/toolchains/gcc-linaro-4.9.4-2017.01-x86_64_arm-linux-gnueabihf/bin/arm-linux-gnueabihf-"

# 清理函数
clean() {
    echo "Cleaning build environment..."
    rm -rf "$OUTPUT_DIR" 2>/dev/null || true
    make -C "$PROJECT_ROOT/board/imx6ull-mini/uboot-imx-rel_imx_4.1.15_2.1.0_ga_alientek" distclean &>/dev/null
    make -C "$PROJECT_ROOT/kernel/linux-imx-rel_imx_4.1.15_2.1.0_ga_alientek" distclean &>/dev/null
    echo "Clean completed."
}

# 编译U-Boot
build_uboot() {
    echo "Building U-Boot..."
    pushd "$PROJECT_ROOT/board/imx6ull-mini/uboot-imx-rel_imx_4.1.15_2.1.0_ga_alientek" >/dev/null
    make ARCH=arm CROSS_COMPILE="$TOOLCHAIN" mx6ull_alientek_emmc_defconfig
    make ARCH=arm CROSS_COMPILE="$TOOLCHAIN" -j$(nproc)
    mkdir -p "$OUTPUT_DIR/uboot"
    cp u-boot.bin "$OUTPUT_DIR/uboot/"
    popd >/dev/null
    echo "U-Boot build completed."
}

# 编译Kernel
build_kernel() {
    echo "Building Linux Kernel..."
    pushd "$PROJECT_ROOT/kernel/linux-imx-rel_imx_4.1.15_2.1.0_ga_alientek" >/dev/null
    make ARCH=arm CROSS_COMPILE="$TOOLCHAIN" imx_alientek_emmc_defconfig
    make ARCH=arm CROSS_COMPILE="$TOOLCHAIN" -j$(nproc) zImage dtbs
    mkdir -p "$OUTPUT_DIR/kernel"
    cp arch/arm/boot/zImage "$OUTPUT_DIR/kernel/"
    cp arch/arm/boot/dts/imx6ull-alientek-emmc.dtb "$OUTPUT_DIR/kernel/"
    popd >/dev/null
    echo "Kernel build completed."
}

# 同步根文件系统
sync_rootfs() {
    echo "Syncing rootfs for NFS..."
    mkdir -p "$OUTPUT_DIR/rootfs"
    rsync -aq --delete "$PROJECT_ROOT/rootfs/" "$OUTPUT_DIR/rootfs/"
    echo "RootFS sync completed."
}

# 参数处理
if [ "$1" == "-c" ]; then
    clean
    exit 0
fi

# 主构建流程
mkdir -p "$OUTPUT_DIR"
build_uboot
build_kernel
sync_rootfs

echo -e "\nBuild completed! Output files are in:"
echo "U-Boot:    $OUTPUT_DIR/uboot/u-boot.bin"
echo "Kernel:    $OUTPUT_DIR/kernel/zImage"
echo "DeviceTree:$OUTPUT_DIR/kernel/imx6ull-14x14-evk.dtb"
echo "RootFS:    $OUTPUT_DIR/rootfs"