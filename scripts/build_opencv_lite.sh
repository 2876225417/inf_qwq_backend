#!/usr/bin/env bash
set -euo pipefail

# OpenCV Building Scripts
# Usage: Download, Configure and Install OpenCV

# Configurations
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
OPENCV_VERSION="4.11.0"
INSTALL_PREFIX="/usr/local" # /usr/local Default installation directory 
BUILD_DIR="${SCRIPT_DIR}/opencv-build"
SOURCE_DIR="${BUILD_DIR}/source"
JOBS=$(nproc || sysctl -n hw.ncpu || echo 4)
BUILD_TYPE="Release"
WITH_CONTRIB="no"
ENABLE_CUDA="no"
ENABLE_PYTHON="no"

# Ouput with color
GREEN='\033[0;32m'
RED='\033[0;31m'
BLUE='\033[0;34m'
NC='\033[0m'

log() {
    echo -e "${BLUE}[$(date '+%Y-%m-%d %H:%M:%S')] ${GREEN}$1${NC}"
}

error() {
    echo -e "${RED}[ERROR] $1${NC}" >&2
    exit 1
}

# 检查是否有足够权限
check_permissions() {
    if [ ! -w "$INSTALL_PREFIX" ]; then
        log "安装到系统目录 $INSTALL_PREFIX 需要管理员权限"
        if [ "$(id -u)" -ne 0 ]; then
            error "请使用 sudo 运行此脚本"
        fi
    fi
}

# 下载 OpenCV
download_opencv() {
    log "下载 OpenCV ${OPENCV_VERSION}..."
    
    # 创建下载目录
    mkdir -p "${BUILD_DIR}"
    
    # 下载 OpenCV 主仓库
    local opencv_url="https://github.com/opencv/opencv/archive/${OPENCV_VERSION}.tar.gz"
    local opencv_tar="${BUILD_DIR}/opencv-${OPENCV_VERSION}.tar.gz"
    
    if [ -f "${opencv_tar}" ]; then
        log "已存在 OpenCV ${OPENCV_VERSION} 源码包，跳过下载"
    else
        log "下载 OpenCV ${OPENCV_VERSION} 源码包..."
        if command -v wget &> /dev/null; then
            wget -O "${opencv_tar}" "${opencv_url}" || error "下载 OpenCV 失败"
        elif command -v curl &> /dev/null; then
            curl -L "${opencv_url}" -o "${opencv_tar}" || error "下载 OpenCV 失败"
        else
            error "未找到 wget 或 curl，无法下载文件"
        fi
    fi
    
    # 如果启用 contrib 模块，也下载它
    if [ "${WITH_CONTRIB}" = "yes" ]; then
        local contrib_url="https://github.com/opencv/opencv_contrib/archive/${OPENCV_VERSION}.tar.gz"
        local contrib_tar="${BUILD_DIR}/opencv_contrib-${OPENCV_VERSION}.tar.gz"
        
        if [ -f "${contrib_tar}" ]; then
            log "已存在 OpenCV Contrib ${OPENCV_VERSION} 源码包，跳过下载"
        else
            log "下载 OpenCV Contrib ${OPENCV_VERSION} 源码包..."
            if command -v wget &> /dev/null; then
                wget -O "${contrib_tar}" "${contrib_url}" || error "下载 OpenCV Contrib 失败"
            elif command -v curl &> /dev/null; then
                curl -L "${contrib_url}" -o "${contrib_tar}" || error "下载 OpenCV Contrib 失败"
            else
                error "未找到 wget 或 curl，无法下载文件"
            fi
        fi
    fi
    
    # 解压 OpenCV 源码
    log "解压 OpenCV 源码..."
    mkdir -p "${SOURCE_DIR}"
    rm -rf "${SOURCE_DIR:?}/"*  # 清空源码目录
    mkdir -p "${SOURCE_DIR}/opencv"
    tar -xf "${opencv_tar}" -C "${SOURCE_DIR}/opencv" --strip-components=1 || error "解压 OpenCV 失败"
    
    # 如果启用 contrib 模块，也解压它
    if [ "${WITH_CONTRIB}" = "yes" ]; then
        log "解压 OpenCV Contrib 源码..."
        mkdir -p "${SOURCE_DIR}/opencv_contrib"
        tar -xf "${contrib_tar}" -C "${SOURCE_DIR}/opencv_contrib" --strip-components=1 || error "解压 OpenCV Contrib 失败"
    fi
    
    log "OpenCV ${OPENCV_VERSION} 源码准备完成"
}

# 构建 OpenCV
build_opencv() {
    log "准备构建 OpenCV ${OPENCV_VERSION}..."
    
    # 设置编译器为 GCC
    export CXX=g++
    export CC=gcc
    
    # 创建构建目录
    mkdir -p "${BUILD_DIR}/build"
    cd "${BUILD_DIR}/build"
    
    # 基本 CMake 选项
    local cmake_options=(
        "-DCMAKE_INSTALL_PREFIX=${INSTALL_PREFIX}"
        "-DCMAKE_BUILD_TYPE=${BUILD_TYPE}"
        "-DBUILD_EXAMPLES=OFF"
        "-DBUILD_TESTS=OFF"
        "-DBUILD_PERF_TESTS=OFF"
        "-DBUILD_DOCS=OFF"
        "-DWITH_FFMPEG=ON"
        "-DWITH_PNG=ON"
        "-DWITH_JPEG=ON"
        "-DWITH_TIFF=ON"
        "-DWITH_WEBP=ON"
        "-DBUILD_opencv_apps=OFF"
        "-DBUILD_SHARED_LIBS=ON"
        "-DBUILD_JAVA=OFF"
        "-DBUILD_opencv_java=OFF"
        "-DBUILD_opencv_java_bindings_generator=OFF"
        "-DCMAKE_CXX_STANDARD=17"
        "-DCMAKE_C_COMPILER=${CC}"
        "-DCMAKE_CXX_COMPILER=${CXX}"
    )
    
    # 如果启用 contrib 模块，添加相应选项
    if [ "${WITH_CONTRIB}" = "yes" ]; then
        cmake_options+=(
            "-DOPENCV_EXTRA_MODULES_PATH=${SOURCE_DIR}/opencv_contrib/modules"
        )
    fi
    
    # 如果启用 CUDA，添加相应选项
    if [ "${ENABLE_CUDA}" = "yes" ]; then
        cmake_options+=(
            "-DWITH_CUDA=ON"
            "-DCUDA_FAST_MATH=ON"
            "-DWITH_CUBLAS=ON"
        )
    else
        cmake_options+=(
            "-DWITH_CUDA=OFF"
        )
    fi
    
    # 如果启用 Python 绑定，添加相应选项
    if [ "${ENABLE_PYTHON}" = "yes" ]; then
        cmake_options+=(
            "-DBUILD_opencv_python3=ON"
            "-DPYTHON3_EXECUTABLE=$(which python3)"
        )
    else
        cmake_options+=(
            "-DBUILD_opencv_python3=OFF"
        )
    fi
    
    # 配置
    log "配置 OpenCV..."
    cmake "${SOURCE_DIR}/opencv" "${cmake_options[@]}" || error "CMake 配置失败"
    
    # 构建并安装
    log "开始构建并安装 OpenCV (使用 $JOBS 个并行作业)..."
    cmake --build . -j "${JOBS}" --target install || error "构建或安装失败"
    
    log "OpenCV ${OPENCV_VERSION} 构建和安装完成!"
}

# 主函数
main() {
    log "开始构建 OpenCV ${OPENCV_VERSION}..."
    
    # 解析命令行参数
    while [[ $# -gt 0 ]]; do
        case $1 in
            --version=*)
                OPENCV_VERSION="${1#*=}"
                shift
                ;;
            --prefix=*)
                INSTALL_PREFIX="${1#*=}"
                shift
                ;;
            --jobs=*)
                JOBS="${1#*=}"
                shift
                ;;
            --build-type=*)
                BUILD_TYPE="${1#*=}"
                shift
                ;;
            --with-contrib=*)
                WITH_CONTRIB="${1#*=}"
                shift
                ;;
            --enable-cuda=*)
                ENABLE_CUDA="${1#*=}"
                shift
                ;;
            --enable-python=*)
                ENABLE_PYTHON="${1#*=}"
                shift
                ;;
            *)
                error "未知选项: $1"
                ;;
        esac
    done
    
    # 显示配置
    log "配置摘要:"
    log "- OpenCV 版本: ${OPENCV_VERSION}"
    log "- 安装路径: ${INSTALL_PREFIX}"
    log "- 构建目录: ${BUILD_DIR}"
    log "- 构建类型: ${BUILD_TYPE}"
    log "- 包含 Contrib: ${WITH_CONTRIB}"
    log "- 启用 CUDA: ${ENABLE_CUDA}"
    log "- 启用 Python: ${ENABLE_PYTHON}"
    log "- 并行作业: ${JOBS}"
    
    # 检查权限
    check_permissions
    
    # 执行构建步骤
    download_opencv
    build_opencv
    
    log "OpenCV ${OPENCV_VERSION} 已成功安装到 ${INSTALL_PREFIX}"
}

# 执行主函数
main "$@"
