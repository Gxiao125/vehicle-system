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
export CROSS_COMPILE=arm-linux-gnueabihf-

# 组件版本
UBOOT_VERSION="2015.04"
KERNEL_VERSION="4.1.15"

# 部署路径
NFS_PATH=""
TFTP_PATH=""

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
        # 清理驱动模块
        make -C "$PROJECT_ROOT/kernel_transport/can_dma_buffer" clean || true
        # 清理Middleware构建
        rm -rf "$PROJECT_ROOT/Middleware/build" || true
        # 清理App构建
        rm -rf "$PROJECT_ROOT/app/build" || true
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

# 编译can_dma_buffer驱动模块
build_drivers() {
    color_echo "${YELLOW}" "Building CAN DMA Buffer driver..."
    local driver_dir="$PROJECT_ROOT/kernel_transport/can_dma_buffer"
    local kernel_build_dir="$PROJECT_ROOT/kernel/linux-imx-rel_imx_4.1.15_2.1.0_ga_alientek"
    local output_dir="$OUTPUT_DIR/drivers"
    
    pushd "$driver_dir" >/dev/null
    
    # 使用内核构建系统编译外部模块
    make -C "$kernel_build_dir" M=$(pwd) modules || error_exit "Driver build failed"
    
    mkdir -p "$output_dir"
    cp *.ko "$output_dir/"
    check_build "Driver" "$output_dir/flexcan_dma.ko"
    popd >/dev/null
    
    color_echo "${GREEN}" "Driver build completed."
}

# 编译Middleware应用
build_middleware() {
    color_echo "${YELLOW}" "Building Middleware application..."
    local middleware_dir="$PROJECT_ROOT/Middleware"
    local build_dir="$middleware_dir/build"
    local output_dir="$OUTPUT_DIR/middleware"

    # 创建构建目录
    mkdir -p "$build_dir"
    pushd "$build_dir" >/dev/null

    # 配置交叉编译环境
    cmake -DCMAKE_TOOLCHAIN_FILE=../toolchain.cmake \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX="$output_dir" \
        .. || error_exit "CMake configuration failed"

    # 编译并安装
    make -j$(nproc) || error_exit "Middleware build failed"
    make install || error_exit "Middleware install failed"

    check_build "Middleware" "$output_dir/bin/can_service"
    popd >/dev/null

    color_echo "${GREEN}" "Middleware build completed."
}

# 编译应用程序
build_applications() {
    color_echo "${YELLOW}" "Building Applications..."
    local app_dir="$PROJECT_ROOT/app"
    local build_dir="$app_dir/build"
    local output_dir="$OUTPUT_DIR/applications"
    
    # 创建构建目录
    mkdir -p "$build_dir"
    pushd "$build_dir" >/dev/null

    # 配置交叉编译环境
    cmake -DCMAKE_TOOLCHAIN_FILE="$PROJECT_ROOT/Middleware/toolchain.cmake" \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX="$output_dir" \
        -DMIDDLEWARE_DIR="$OUTPUT_DIR/middleware" \
        .. || error_exit "Applications CMake configuration failed"

    # 编译并安装
    make -j$(nproc) || error_exit "Applications build failed"
    make install || error_exit "Applications install failed"

    popd >/dev/null

    color_echo "${GREEN}" "Applications build completed."
}

# 生成交叉编译工具链文件
generate_toolchain_file() {
    color_echo "${YELLOW}" "Generating CMake toolchain file..."
    local middleware_dir="$PROJECT_ROOT/Middleware"
    local toolchain_file="$middleware_dir/toolchain.cmake"
    
    # 创建工具链文件
    cat << EOF > "$toolchain_file"
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR arm)

set(CMAKE_C_COMPILER ${CROSS_COMPILE}gcc)
set(CMAKE_CXX_COMPILER ${CROSS_COMPILE}g++)

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
EOF
    
    color_echo "${GREEN}" "Toolchain file created: $toolchain_file"
}

# 部署到NFS根文件系统
deploy_to_nfs() {
    if [ -z "$NFS_PATH" ]; then
        color_echo "${YELLOW}" "No NFS path specified, skipping NFS deployment."
        return
    fi

    color_echo "${YELLOW}" "Deploying to NFS root filesystem at $NFS_PATH..."
    
    # 创建必要的运行时目录
    for dir in dev proc sys run tmp; do
        sudo mkdir -p "$NFS_PATH/$dir"
        sudo chmod 777 "$NFS_PATH/$dir"
    done
    sudo rm -rf "$NFS_PATH/tmp/*" 2>/dev/null || true
    
    # 部署应用程序
    local app_output_dir="$OUTPUT_DIR/applications"
    sudo mkdir -p "$NFS_PATH/app/body_control"
    sudo mkdir -p "$NFS_PATH/app/brake_system"
    sudo mkdir -p "$NFS_PATH/app/dashboard"
    
    sudo cp "$app_output_dir/bin/body_control" "$NFS_PATH/app/body_control/" || true
    sudo cp "$PROJECT_ROOT/app/body_control/BodyControl.dbc" "$NFS_PATH/app/body_control/" 2>/dev/null || true
    
    sudo cp "$app_output_dir/bin/powertrain" "$NFS_PATH/app/brake_system/" || true
    sudo cp "$PROJECT_ROOT/app/brake_system/Powertrain.dbc" "$NFS_PATH/app/brake_system/" 2>/dev/null || true
    
    sudo cp "$app_output_dir/bin/dashboard" "$NFS_PATH/app/dashboard/" || true
    sudo cp "$PROJECT_ROOT/app/dashboard/Dashboard.dbc" "$NFS_PATH/app/dashboard/" 2>/dev/null || true
    
    # 设置执行权限
    sudo chmod +x "$NFS_PATH/app/body_control/body_control" 2>/dev/null || true
    sudo chmod +x "$NFS_PATH/app/brake_system/powertrain" 2>/dev/null || true
    sudo chmod +x "$NFS_PATH/app/dashboard/dashboard" 2>/dev/null || true
    
    # 部署中间件
    local middleware_output_dir="$OUTPUT_DIR/middleware"
    sudo mkdir -p "$NFS_PATH/usr/local/bin"
    sudo mkdir -p "$NFS_PATH/usr/local/include"
    sudo mkdir -p "$NFS_PATH/usr/local/lib"
    
    sudo cp -f "$middleware_output_dir/bin/"* "$NFS_PATH/usr/local/bin/" 2>/dev/null || true
    sudo cp -f "$middleware_output_dir/include/"*.h "$NFS_PATH/usr/local/include/" 2>/dev/null || true
    sudo cp -f "$middleware_output_dir/lib/"*.a "$NFS_PATH/usr/local/lib/" 2>/dev/null || true
    
    # 部署驱动
    local driver_output_dir="$OUTPUT_DIR/drivers"
    sudo mkdir -p "$NFS_PATH/lib/modules/$KERNEL_VERSION"
    sudo cp -f "$driver_output_dir/"*.ko "$NFS_PATH/lib/modules/$KERNEL_VERSION/" 2>/dev/null || true
    
    color_echo "${GREEN}" "NFS deployment completed to $NFS_PATH"
}

# 部署到TFTP服务器
deploy_to_tftp() {
    if [ -z "$TFTP_PATH" ]; then
        color_echo "${YELLOW}" "No TFTP path specified, skipping TFTP deployment."
        return
    fi

    color_echo "${YELLOW}" "Deploying to TFTP server at $TFTP_PATH..."
    
    # 确保TFTP目录存在
    sudo mkdir -p "$TFTP_PATH"
    
    # 部署U-Boot
    local uboot_output_dir="$OUTPUT_DIR/uboot"
    sudo cp -f "$uboot_output_dir/u-boot.bin" "$TFTP_PATH/" || true
    
    # 部署内核镜像
    local kernel_output_dir="$OUTPUT_DIR/kernel"
    sudo cp -f "$kernel_output_dir/zImage" "$TFTP_PATH/" || true
    
    # 部署设备树文件
    sudo cp -f "$kernel_output_dir/"*.dtb "$TFTP_PATH/" 2>/dev/null || true
    
    color_echo "${GREEN}" "TFTP deployment completed to $TFTP_PATH"
}

# 显示构建报告
show_report() {
    color_echo "${GREEN}" "\nBuild Report:"
    echo "Timestamp:   $BUILD_DATE"
    echo "U-Boot:      $(md5sum $OUTPUT_DIR/uboot/u-boot.bin 2>/dev/null || echo 'N/A')"
    echo "Kernel:      $(md5sum $OUTPUT_DIR/kernel/zImage 2>/dev/null || echo 'N/A')"
    echo "DeviceTree:  $(ls $OUTPUT_DIR/kernel/*.dtb 2>/dev/null | wc -l) files"
    echo "Drivers:     $(ls $OUTPUT_DIR/drivers/*.ko 2>/dev/null | wc -l) modules"
    echo "Middleware:  $(ls $OUTPUT_DIR/middleware/bin/* 2>/dev/null | wc -l) executables"
    echo "Applications: $(find $OUTPUT_DIR/applications/bin -type f 2>/dev/null | wc -l) executables"
    
    if [ -n "$NFS_PATH" ]; then
        color_echo "${YELLOW}" "\nNFS Deployment:"
        echo "Path: $NFS_PATH"
        echo "App dirs: $(ls -d $NFS_PATH/app/* 2>/dev/null | wc -l)"
        echo "Middleware: $(ls $NFS_PATH/usr/local/bin/* 2>/dev/null | wc -l) executables"
    fi
    
    if [ -n "$TFTP_PATH" ]; then
        color_echo "${YELLOW}" "\nTFTP Deployment:"
        echo "Path: $TFTP_PATH"
        echo "Files: $(ls $TFTP_PATH/{u-boot.bin,zImage,*.dtb} 2>/dev/null | wc -l)"
    fi
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
                deploy_to_tftp
                exit 0
                ;;
            -k|--kernel-only)
                build_kernel
                build_drivers
                deploy_to_tftp
                exit 0
                ;;
            -d|--drivers-only)
                build_drivers
                deploy_to_nfs
                exit 0
                ;;
            -m|--middleware-only)
                generate_toolchain_file
                build_middleware
                deploy_to_nfs
                exit 0
                ;;
            -a|--app-only)
                generate_toolchain_file
                build_applications
                deploy_to_nfs
                exit 0
                ;;
            --nfs-path)
                shift
                NFS_PATH=$(realpath "$1")
                ;;
            --tftp-path)
                shift
                TFTP_PATH=$(realpath "$1")
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
    echo "  -c, --clean           Clean build artifacts"
    echo "  -u, --uboot-only      Build U-Boot only (also deploys to TFTP if specified)"
    echo "  -k, --kernel-only     Build Kernel and modules (also deploys to TFTP if specified)"
    echo "  -d, --drivers-only    Build external drivers only (also deploys to NFS if specified)"
    echo "  -m, --middleware-only Build Middleware application only (also deploys to NFS if specified)"
    echo "  -a, --app-only        Build applications only (also deploys to NFS if specified)"
    echo "  --nfs-path PATH       Deploy root filesystem to NFS PATH"
    echo "  --tftp-path PATH      Deploy boot files to TFTP PATH"
    echo "  -h, --help            Show this help"
}

# 主流程
main() {
    init_logging
    parse_args "$@"
    
    color_echo "${YELLOW}" "Starting build process..."
    mkdir -p "$OUTPUT_DIR"

    # 创建符号链接（如果需要）
    mkdir -p "$PROJECT_ROOT/transport/include"
    mkdir -p "$PROJECT_ROOT/transport/driver_can"

    ln -sf "$PROJECT_ROOT/kernel/linux-imx-rel_imx_4.1.15_2.1.0_ga_alientek/drivers/net/can/flexcan.h" "$PROJECT_ROOT/transport/include/flexcan.h" || true
    ln -sf "$PROJECT_ROOT/kernel/linux-imx-rel_imx_4.1.15_2.1.0_ga_alientek/drivers/net/can" "$PROJECT_ROOT/transport/driver_can/" || true

    build_uboot
    build_kernel
    build_drivers
    
    # 为Middleware生成交叉编译工具链文件并构建
    generate_toolchain_file
    build_middleware
    
    # 编译应用程序
    build_applications
    
    # 部署到目标路径
    deploy_to_nfs
    deploy_to_tftp
    
    show_report
}

# 执行入口
main "$@"