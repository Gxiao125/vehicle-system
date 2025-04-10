#!/bin/bash
# 配置参数
OUTPUT_DIR="value"
ROOTFS_SYNC_DIR="/home/user/nfs_rootfs"
# 修改为明确的工具链路径 ▼▼▼
TOOLCHAIN_DIR="${PWD}/tools/gcc-linaro-4.9.4-2017.01-x86_64_arm-linux-gnueabihf"
CROSS_COMPILE_PATH="${TOOLCHAIN_DIR}/bin/arm-linux-gnueabihf-"
# ▲▲▲ 这里需要确认实际路径是否匹配
UBOOT_DIR="bsp/uboot-imx-rel_imx_4.1.15_2.1.0_ga_alientek"
KERNEL_DIR="bsp/linux-imx-rel_imx_4.1.15_2.1.0_ga_alientek"

# 参数处理
CLEAN_MODE=0
SKIP_BUILD=0
while getopts "cs" opt; do
  case $opt in
    c) CLEAN_MODE=1 ;;
    s) SKIP_BUILD=1 ;;
    *) echo "Usage: $0 [-c] [-s]"; exit 1 ;;
  esac
done

# 创建输出目录（如果不存在）
mkdir -p ${OUTPUT_DIR}

# 清理模式处理
if [ ${CLEAN_MODE} -eq 1 ]; then
  echo "Clean mode: Removing all files in ${OUTPUT_DIR}"
  rm -rf ${OUTPUT_DIR}/*
fi

check_toolchain() {
  if [ ! -x "${TOOLCHAIN_DIR}/bin/arm-linux-gnueabihf-gcc" ]; then
    echo -e "\033[31m[ERROR] 交叉编译器未找到！\033[0m"
    echo "请确认以下路径是否存在："
    echo ${TOOLCHAIN_DIR}
    echo "或执行以下命令解压工具链："
    echo "tar -xvf tools/gcc-linaro-4.9.4-2017.01-x86_64_arm-linux-gnueabihf.tar.xz -C tools/"
    exit 1
  fi
}

# 导出环境变量
export ARCH=arm
export CROSS_COMPILE=${CROSS_COMPILE_PATH}
export TOP_PROJECT_PATH=${PWD}/
export PATH=${PATH}:${TOOLCHAIN_DIR}/bin  # 关键修复点！

########################################
# 编译 U-Boot
########################################
compile_uboot() {
  echo "Building U-Boot..."
  cd ${UBOOT_DIR} || exit 1
  
  # 根据需求选择清理或增量编译
  if [ ${CLEAN_MODE} -eq 1 ]; then
    make distclean
    make mx6ull_alientek_emmc_defconfig
  fi
  
  make V=1 -j$(nproc)
  
  if [ $? -eq 0 ]; then
    cp u-boot.imx ../${OUTPUT_DIR}/u-boot.bin
    echo "U-Boot build successful!"
  else
    echo "U-Boot build failed!"
    exit 1
  fi
  
  cd - > /dev/null || exit 1
}

########################################
# 编译 Kernel
########################################
compile_kernel() {
  echo "Building Kernel..."
  cd ${KERNEL_DIR} || exit 1
  
  # 根据需求选择清理或增量编译
  if [ ${CLEAN_MODE} -eq 1 ]; then
    make distclean
    make imx_alientek_emmc_defconfig
  fi
  
  make all -j$(nproc)
  
  if [ $? -eq 0 ]; then
    cp arch/arm/boot/zImage ../${OUTPUT_DIR}/
    cp arch/arm/boot/dts/imx6ull-alientek-emmc.dtb ../${OUTPUT_DIR}/
    echo "Kernel build successful!"
  else
    echo "Kernel build failed!"
    exit 1
  fi
  
  cd - > /dev/null || exit 1
}

########################################
# 同步文件系统（添加sudo权限）
########################################
sync_rootfs() {
  sudo makdir -p ${ROOTFS_SYNC_DIR}
  echo "Syncing rootfs to ${ROOTFS_SYNC_DIR}"
  sudo rsync -avz --delete \
    --exclude=".git" \
    --exclude="*.swp" \
    rootfs/ ${ROOTFS_SYNC_DIR}
}

# 主执行流程
if [ ${SKIP_BUILD} -eq 0 ]; then
  compile_uboot
  compile_kernel
fi

sync_rootfs

echo "Build completed! Output files:"
ls -lh ${OUTPUT_DIR}/
