#!/usr/bin/env bash
set -euo pipefail

# Boost Building Scripts
# Usage: Donwload, Build and Install Boost

# Configurations
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BOOST_VERSION="1.87.0"
INSTALL_PREFIX="/usr/local" # /usr/local Default installation directory 
BUILD_DIR="${SCRIPT_DIR}/boost-build"
SOURCE_DIR="${BUILD_DIR}/source"
JOBS=$(nproc || sysctl -n hw.ncpu || echo 4)
BUILD_SHARED="yes"
BUILD_STATIC="yes"
BUILD_TYPE="release"
COMPONENTS=""

# 颜色输出
GREEN='\033[0;32m'
RED='\033[0;31m'
BLUE='\033[0;34m'
NC='\033[0m'

# 打印带颜色的消息
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

# 下载 Boost
download_boost() {
    log "下载 Boost ${BOOST_VERSION}..."
    
    mkdir -p "${BUILD_DIR}"
    
    local version_underscore="${BOOST_VERSION//./_}"
    local download_url="https://archives.boost.io/release/${BOOST_VERSION}/source/boost_${version_underscore}.tar.gz"
    local tar_file="${BUILD_DIR}/boost_${version_underscore}.tar.gz"
    
    if [ -f "${tar_file}" ]; then
        log "已存在 Boost ${BOOST_VERSION} 源码包，跳过下载"
    else
        log "下载 Boost ${BOOST_VERSION} 源码包..."
        if command -v wget &> /dev/null; then
            wget -O "${tar_file}" "${download_url}" || error "下载失败"
        elif command -v curl &> /dev/null; then
            curl -L "${download_url}" -o "${tar_file}" || error "下载失败"
        else
            error "未找到 wget 或 curl，无法下载文件"
        fi
    fi
    
    # 解压源码
    log "解压 Boost 源码..."
    mkdir -p "${SOURCE_DIR}"
    rm -rf "${SOURCE_DIR:?}/"*
    tar -xf "${tar_file}" -C "${SOURCE_DIR}" --strip-components=1 || error "解压失败"
    
    log "Boost ${BOOST_VERSION} 源码准备完成"
}

# 构建 Boost
build_boost() {
    log "准备构建 Boost ${BOOST_VERSION}..."
    cd "${SOURCE_DIR}"
    
    # 设置编译器为 GCC
    export CXX=g++
    export CC=gcc
    
    # 构建 b2 工具（bootstrap）
    if [ ! -f "./b2" ]; then
        log "运行 bootstrap..."
        ./bootstrap.sh --prefix="${INSTALL_PREFIX}" || error "Bootstrap 失败"
    fi
    
    # 配置构建选项
    local b2_options=(
        "--prefix=${INSTALL_PREFIX}"
        "--build-dir=${BUILD_DIR}/build"
        "-j${JOBS}"
        "threading=multi"
        "variant=${BUILD_TYPE}"
        "--layout=tagged"
        "toolset=gcc"
    )
    
    # 添加链接类型选项
    if [ "${BUILD_SHARED}" = "yes" ] && [ "${BUILD_STATIC}" = "yes" ]; then
        b2_options+=("link=shared,static")
    elif [ "${BUILD_SHARED}" = "yes" ]; then
        b2_options+=("link=shared")
    elif [ "${BUILD_STATIC}" = "yes" ]; then
        b2_options+=("link=static")
    fi
    
    # 运行时链接选项
    if [ "${BUILD_SHARED}" = "yes" ]; then
        b2_options+=("runtime-link=shared")
    else
        b2_options+=("runtime-link=static")
    fi
    
    # 添加特定平台的优化
    case "$(uname -s)" in
        Linux*)
            b2_options+=("cxxflags=-fPIC")
            ;;
        Darwin*)
            b2_options+=("cxxflags=-fPIC -mmacosx-version-min=10.14")
            ;;
    esac
    
    # 添加特定组件构建
    if [ -n "${COMPONENTS}" ]; then
        log "仅构建以下组件: ${COMPONENTS}"
        b2_options+=("--with-${COMPONENTS//,/ --with-}")
    fi
    
    # 构建和安装
    log "开始构建 Boost (使用 $JOBS 个并行作业)..."
    log "构建选项: ${b2_options[*]}"
    
    # 直接执行构建和安装
    ./b2 "${b2_options[@]}" install || error "构建失败"
    
    log "Boost ${BOOST_VERSION} 构建和安装完成!"
}

# 主函数
main() {
    log "开始构建 Boost ${BOOST_VERSION}..."
    
    # 解析命令行参数
    while [[ $# -gt 0 ]]; do
        case $1 in
            --version=*)
                BOOST_VERSION="${1#*=}"
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
            --shared=*)
                BUILD_SHARED="${1#*=}"
                shift
                ;;
            --static=*)
                BUILD_STATIC="${1#*=}"
                shift
                ;;
            --type=*)
                BUILD_TYPE="${1#*=}"
                shift
                ;;
            --components=*)
                COMPONENTS="${1#*=}"
                shift
                ;;
            *)
                error "未知选项: $1"
                ;;
        esac
    done
    
    # 显示配置
    log "配置摘要:"
    log "- Boost 版本: ${BOOST_VERSION}"
    log "- 安装路径: ${INSTALL_PREFIX}"
    log "- 构建目录: ${BUILD_DIR}"
    log "- 构建类型: ${BUILD_TYPE}"
    log "- 构建共享库: ${BUILD_SHARED}"
    log "- 构建静态库: ${BUILD_STATIC}"
    log "- 并行作业: ${JOBS}"
    if [ -n "${COMPONENTS}" ]; then
        log "- 指定组件: ${COMPONENTS}"
    else
        log "- 构建所有组件"
    fi
    
    # 检查权限
    check_permissions
    
    # 执行构建步骤
    download_boost
    build_boost
    
    log "Boost ${BOOST_VERSION} 已成功安装到 ${INSTALL_PREFIX}"
}

# 执行主函数
main "$@"
