#!/bin/bash
set -eo pipefail

# 启用彩色输出
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

# 初始化环境
SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" &> /dev/null && pwd)
PROJECT_ROOT=$(realpath "$SCRIPT_DIR/..")
OUTPUT_DIR="$PROJECT_ROOT/output"
LOG_DIR="$OUTPUT_DIR/logs"
BUILD_DATE=$(date +"%Y%m%d-%H%M%S")

# 加载环境配置
source "$PROJECT_ROOT/build/config.env"

# 工具链配置
export ARCH=arm
export CROSS_COMPILE="$PROJECT_ROOT/build/toolchains/gcc-linaro-4.9.4-2017.01-x86_64_arm-linux-gnueabihf/bin/arm-linux-gnueabihf-"

# 组件版本
UBOOT_VERSION="2015.04"
KERNEL_VERSION="4.1.15"

# 初始化日志系统
init_logging() {
    mkdir -p "$LOG_DIR"
    exec 3>&1 4>&2
    exec > >(tee "$LOG_DIR/build_${BUILD_DATE}.log") 2>&1
}

# 带颜色输出函数
color_echo() {
    local color=$1
    shift
    echo -e "${color}$*${NC}"
}

# 错误处理函数
error_exit() {
    color_echo "${RED}" "Error: $*"
    exit 1
}

# 清理函数
clean() {
    color_echo "${YELLOW}" "Cleaning build environment..."
    {
        rm -rf "$OUTPUT_DIR" || true
        make -C "$PROJECT_ROOT/board/imx6ull-mini/uboot-imx-rel_imx_4.1.15_2.1.0_ga_alientek" distclean
        make -C "$PROJECT_ROOT/kernel/linux-imx-rel_imx_4.1.15_2.1.0_ga_alientek" distclean
    } > /dev/null 2>&1
    color_echo "${GREEN}" "Clean completed."
}

# 编译状态检查
check_build() {
    local component=$1
    local target=$2
    [ -f "$target" ] || error_exit "$component build failed! Check $LOG_DIR"
}

# 编译U-Boot
build_uboot() {
    color_echo "${YELLOW}" "Building U-Boot ${UBOOT_VERSION}..."
    local uboot_dir="$PROJECT_ROOT/board/imx6ull-mini/uboot-imx-rel_imx_4.1.15_2.1.0_ga_alientek"
    local output_dir="$OUTPUT_DIR/uboot"
    
    pushd "$uboot_dir" >/dev/null
    make ARCH=arm mx6ull_alientek_emmc_defconfig || error_exit "U-Boot config failed"
    make ARCH=arm -j$(nproc) || error_exit "U-Boot build failed"
    
    mkdir -p "$output_dir"
    cp u-boot.bin "$output_dir/"
    check_build "U-Boot" "$output_dir/u-boot.bin"
    popd >/dev/null
    
    color_echo "${GREEN}" "U-Boot build completed."
}

# 编译Kernel及模块
build_kernel() {
    color_echo "${YELLOW}" "Building Linux Kernel ${KERNEL_VERSION}..."
    local kernel_dir="$PROJECT_ROOT/kernel/linux-imx-rel_imx_4.1.15_2.1.0_ga_alientek"
    local output_dir="$OUTPUT_DIR/kernel"
    local modules_dir="$OUTPUT_DIR/modules"
    
    pushd "$kernel_dir" >/dev/null
    make ARCH=arm imx_alientek_emmc_defconfig || error_exit "Kernel config failed"
    
    # 编译内核镜像
    make ARCH=arm -j$(nproc) zImage dtbs || error_exit "Kernel build failed"
    
    # 编译内核模块
    make ARCH=arm modules -j$(nproc) || error_exit "Kernel modules build failed"
    
    # 安装模块到输出目录
    make ARCH=arm INSTALL_MOD_PATH="$modules_dir" modules_install || error_exit "Modules install failed"
    
    mkdir -p "$output_dir"
    cp arch/arm/boot/zImage "$output_dir/"
    cp arch/arm/boot/dts/imx6ull-*.dtb "$output_dir/"
    check_build "Kernel" "$output_dir/zImage"
    popd >/dev/null
    
    color_echo "${GREEN}" "Kernel build completed."
}

# 编译外部驱动模块
build_drivers() {
    color_echo "${YELLOW}" "Building external drivers..."
    local drivers_dir="$PROJECT_ROOT/drivers/kernel"
    local modules_dir="$OUTPUT_DIR/modules"
    
    [ -d "$drivers_dir" ] || return 0
    
    find "$drivers_dir" -name "Makefile" | while read makefile; do
        local mod_dir=$(dirname "$makefile")
        color_echo "${YELLOW}" "Building module in $mod_dir"
        
        make -C "$kernel_dir" M="$mod_dir" modules || error_exit "Driver build failed in $mod_dir"
        make -C "$kernel_dir" M="$mod_dir" INSTALL_MOD_PATH="$modules_dir" modules_install
    done
    
    color_echo "${GREEN}" "Drivers build completed."
}

# 同步根文件系统
# sync_rootfs() {
#     color_echo "${YELLOW}" "Preparing root filesystem..."
#     local rootfs_src="$PROJECT_ROOT/rootfs"
#     local rootfs_dest="$OUTPUT_DIR/rootfs"
    
#     [ -d "$rootfs_src" ] || error_exit "RootFS source directory not found"
    
#     rsync -aq --delete "$rootfs_src/" "$rootfs_dest/"
    
#     # 部署内核模块
#     if [ -d "$OUTPUT_DIR/modules" ]; then
#         rsync -a "$OUTPUT_DIR/modules/lib/" "$rootfs_dest/"
#     fi
    
#     color_echo "${GREEN}" "RootFS prepared: $rootfs_dest"
# }

# 显示构建报告
show_report() {
    color_echo "${GREEN}" "\nBuild Report:"
    echo "Timestamp:   $BUILD_DATE"
    echo "U-Boot:      $(md5sum $OUTPUT_DIR/uboot/u-boot.bin 2>/dev/null || echo 'N/A')"
    echo "Kernel:      $(md5sum $OUTPUT_DIR/kernel/zImage 2>/dev/null || echo 'N/A')"
    echo "DeviceTree:  $(ls $OUTPUT_DIR/kernel/*.dtb 2>/dev/null | wc -l) files"
    echo "RootFS Size: $(du -sh $OUTPUT_DIR/rootfs 2>/dev/null || echo 'N/A')"
}

# 参数解析
parse_args() {
    while [[ $# -gt 0 ]]; do
        case $1 in
            -c|--clean)
                clean
                exit 0
                ;;
            -u|--uboot-only)
                build_uboot
                exit 0
                ;;
            -k|--kernel-only)
                build_kernel
                build_drivers
                exit 0
                ;;
            -d|--drivers-only)
                build_drivers
                exit 0
                ;;
            -h|--help)
                show_help
                exit 0
                ;;
            *)
                error_exit "Unknown option: $1"
                ;;
        esac
        shift
    done
}

show_help() {
    echo "Usage: $0 [options]"
    echo "Options:"
    echo "  -c, --clean        Clean build artifacts"
    echo "  -u, --uboot-only   Build U-Boot only"
    echo "  -k, --kernel-only  Build Kernel and modules"
    echo "  -d, --drivers-only Build external drivers only"
    echo "  -h, --help         Show this help"
}

# 主流程
main() {
    init_logging
    parse_args "$@"
    
    color_echo "${YELLOW}" "Starting build process..."
    mkdir -p "$OUTPUT_DIR"
    
    build_uboot
    build_kernel
    build_drivers
    # sync_rootfs
    
    show_report
}

# 执行入口
main "$@"