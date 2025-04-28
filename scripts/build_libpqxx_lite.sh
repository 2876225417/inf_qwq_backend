#!/usr/bin/env bash
set -euo pipefail

# libpqxx Building Script
# Usage: Download, Configure and Install libpqxx

# Configurations
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
LIBPQXX_VERSION="7.9.2"
INSTALL_PREFIX="/usr/local" # Default installation directory 
BUILD_DIR="${SCRIPT_DIR}/libpqxx-build"
SOURCE_DIR="${BUILD_DIR}/source"
JOBS=$(nproc || sysctl -n hw.ncpu || echo 4)
BUILD_TYPE="Release"

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

# 检查依赖
check_dependencies() {
    log "检查依赖项..."
    
    # 检查编译工具
    command -v g++ >/dev/null 2>&1 || error "未找到 g++，请安装 GCC"
    command -v cmake >/dev/null 2>&1 || error "未找到 cmake，请安装 CMake"
    
    # 检查 PostgreSQL 开发库
    pkg-config --exists libpq || {
        error "未找到 libpq 开发库，请安装 PostgreSQL 开发包"
    }
    
    log "所有依赖项检查通过"
}

# 下载 libpqxx
download_libpqxx() {
    log "下载 libpqxx ${LIBPQXX_VERSION}..."
    
    # 创建下载目录
    mkdir -p "${BUILD_DIR}"
    
    # 下载 libpqxx
    local libpqxx_url="https://github.com/jtv/libpqxx/archive/refs/tags/${LIBPQXX_VERSION}.tar.gz"
    local libpqxx_tar="${BUILD_DIR}/libpqxx-${LIBPQXX_VERSION}.tar.gz"
    
    if [ -f "${libpqxx_tar}" ]; then
        log "已存在 libpqxx ${LIBPQXX_VERSION} 源码包，跳过下载"
    else
        log "下载 libpqxx ${LIBPQXX_VERSION} 源码包..."
        if command -v wget &> /dev/null; then
            wget -O "${libpqxx_tar}" "${libpqxx_url}" || error "下载 libpqxx 失败"
        elif command -v curl &> /dev/null; then
            curl -L "${libpqxx_url}" -o "${libpqxx_tar}" || error "下载 libpqxx 失败"
        else
            error "未找到 wget 或 curl，无法下载文件"
        fi
    fi
    
    # 解压 libpqxx 源码
    log "解压 libpqxx 源码..."
    mkdir -p "${SOURCE_DIR}"
    rm -rf "${SOURCE_DIR:?}/"*  # 清空源码目录
    mkdir -p "${SOURCE_DIR}/libpqxx"
    tar -xf "${libpqxx_tar}" -C "${SOURCE_DIR}/libpqxx" --strip-components=1 || error "解压 libpqxx 失败"
    
    log "libpqxx ${LIBPQXX_VERSION} 源码准备完成"
}

# 构建 libpqxx
build_libpqxx() {
    log "准备构建 libpqxx ${LIBPQXX_VERSION}..."
    
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
        "-DBUILD_SHARED_LIBS=ON"
        "-DBUILD_DOC=OFF"
        "-DBUILD_TEST=OFF"
        "-DCMAKE_CXX_STANDARD=17"
        "-DCMAKE_C_COMPILER=${CC}"
        "-DCMAKE_CXX_COMPILER=${CXX}"
        "-DSKIP_BUILD_TEST=ON"
    )
    
    # 配置
    log "配置 libpqxx..."
    cmake "${SOURCE_DIR}/libpqxx" "${cmake_options[@]}" || error "CMake 配置失败"
    
    # 构建并安装
    log "开始构建并安装 libpqxx (使用 $JOBS 个并行作业)..."
    cmake --build . -j "${JOBS}" --target install || error "构建或安装失败"
    
    log "libpqxx ${LIBPQXX_VERSION} 构建和安装完成!"
}

# 主函数
main() {
    log "开始构建 libpqxx ${LIBPQXX_VERSION}..."
    
    # 解析命令行参数
    while [[ $# -gt 0 ]]; do
        case $1 in
            --version=*)
                LIBPQXX_VERSION="${1#*=}"
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
            *)
                error "未知选项: $1"
                ;;
        esac
    done
    
    # 显示配置
    log "配置摘要:"
    log "- libpqxx 版本: ${LIBPQXX_VERSION}"
    log "- 安装路径: ${INSTALL_PREFIX}"
    log "- 构建目录: ${BUILD_DIR}"
    log "- 构建类型: ${BUILD_TYPE}"
    log "- 并行作业: ${JOBS}"
    
    # 检查权限
    check_permissions
    
    # 检查依赖
    check_dependencies
    
    # 执行构建步骤
    download_libpqxx
    build_libpqxx
    
    log "libpqxx ${LIBPQXX_VERSION} 已成功安装到 ${INSTALL_PREFIX}"
}

# 执行主函数
main "$@"