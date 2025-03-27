#!/usr/bin/env bash
set -euo pipefail

# Boost 构建脚本
# 用途: 下载、构建并安装 Boost 到 3rdparty/boost 目录

# 配置参数
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BOOST_VERSION="1.87.0"  # 可以根据需要更改版本
INSTALL_PREFIX="${SCRIPT_DIR}/3rdparty/boost"
BUILD_DIR="${SCRIPT_DIR}/boost-build"
SOURCE_DIR="${BUILD_DIR}/source"
JOBS=$(nproc || sysctl -n hw.ncpu || echo 4)  # 并行构建数量

# 颜色输出
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[0;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

# 打印带颜色的消息
log() {
    echo -e "${BLUE}[$(date '+%Y-%m-%d %H:%M:%S')] ${GREEN}$1${NC}"
}

error() {
    echo -e "${RED}[ERROR] $1${NC}" >&2
    exit 1
}

warn() {
    echo -e "${YELLOW}[WARNING] $1${NC}" >&2
}

info() {
    echo -e "${CYAN}[INFO] $1${NC}"
}

# 检查必要工具
check_prerequisites() {
    log "检查必要工具..."
    
    local missing_tools=()
    
    # 检查必要工具
    for tool in wget curl tar make; do
        if ! command -v $tool &> /dev/null; then
            missing_tools+=($tool)
        fi
    done
    
    if [ ${#missing_tools[@]} -ne 0 ]; then
        error "缺少必要工具: ${missing_tools[*]}\n请先安装这些工具后再运行此脚本"
    fi
    
    # 检查编译器
    if command -v clang++ &> /dev/null; then
        export CXX=clang++
        export CC=clang
        log "使用 Clang 编译器: $(clang++ --version | head -n 1)"
    elif command -v g++ &> /dev/null; then
        export CXX=g++
        export CC=gcc
        log "使用 GCC 编译器: $(g++ --version | head -n 1)"
    else
        error "未找到可用的 C++ 编译器"
    fi
    
    # 检查自定义 Clang
    if [ -d "${SCRIPT_DIR}/clang" ] && [ -f "${SCRIPT_DIR}/clang/bin/clang++" ]; then
        export CXX="${SCRIPT_DIR}/clang/bin/clang++"
        export CC="${SCRIPT_DIR}/clang/bin/clang"
        export PATH="${SCRIPT_DIR}/clang/bin:$PATH"
        export LD_LIBRARY_PATH="${SCRIPT_DIR}/clang/lib:$LD_LIBRARY_PATH"
        log "使用自定义 Clang 编译器: $(${CXX} --version | head -n 1)"
    fi
    
    # 显示并行作业数
    info "将使用 $JOBS 个并行作业进行构建"
}

# 下载 Boost
download_boost() {
    log "下载 Boost ${BOOST_VERSION}..."
    
    # 创建下载目录
    mkdir -p "${BUILD_DIR}"
    
    # 格式化版本号用于下载
    local version_underscore="${BOOST_VERSION//./_}"
    local download_url="https://archives.boost.io/release/${BOOST_VERSION}/source/boost_${version_underscore}.tar.gz"
    local tar_file="${BUILD_DIR}/boost_${version_underscore}.tar.gz"
    
    # 下载 Boost 源码
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
    rm -rf "${SOURCE_DIR:?}/"*  # 清空源码目录
    tar -xf "${tar_file}" -C "${SOURCE_DIR}" --strip-components=1 || error "解压失败"
    
    log "Boost ${BOOST_VERSION} 源码准备完成"
}

# 构建 Boost
build_boost() {
    log "准备构建 Boost ${BOOST_VERSION}..."
    cd "${SOURCE_DIR}"
    
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
        "install"
        "threading=multi"
        "link=shared,static"
        "runtime-link=shared"
        "variant=release"
        "--layout=tagged"
    )
    
    # 检查是否使用 Clang
    if [[ "$CXX" == *"clang++"* ]]; then
        b2_options+=("toolset=clang")
        log "使用 Clang 工具链构建 Boost"
    fi
    
    # 记录开始时间
    local start_time=$(date +%s)
    
    # 构建和安装
    log "开始构建 Boost (使用 $JOBS 个并行作业)..."
    info "构建可能需要一些时间，请耐心等待..."
    ./b2 "${b2_options[@]}" || error "构建失败"
    
    # 计算构建时间
    local end_time=$(date +%s)
    local duration=$((end_time - start_time))
    local minutes=$((duration / 60))
    local seconds=$((duration % 60))
    
    log "Boost ${BOOST_VERSION} 构建完成，耗时: ${minutes}分钟 ${seconds}秒"
}

# 安装完成后的处理
finish_install() {
    log "Boost 已安装到 ${INSTALL_PREFIX}"
    
    # 创建版本文件，便于后续检查
    echo "${BOOST_VERSION}" > "${INSTALL_PREFIX}/version.txt"
    
    # 显示安装的库
    log "已安装的 Boost 库:"
    find "${INSTALL_PREFIX}/lib" -name "libboost_*.so" -o -name "libboost_*.a" | sed 's/.*libboost_\(.*\)\.so.*/\1/' | sort | uniq | tr '\n' ', '
    echo ""
    
    log "要在 CMake 项目中使用 Boost，可以添加以下内容到 CMakeLists.txt:"
    echo "  set(BOOST_ROOT \"${INSTALL_PREFIX}\")"
    echo "  find_package(Boost REQUIRED COMPONENTS system filesystem ...)"
    echo "  target_link_libraries(your_target PRIVATE \${Boost_LIBRARIES})"
}

# 清理函数
cleanup() {
    log "是否要删除构建目录以节省磁盘空间? (y/N)"
    read -r response
    if [[ "$response" =~ ^([yY][eE][sS]|[yY])$ ]]; then
        log "删除构建目录..."
        rm -rf "${BUILD_DIR}"
        log "构建目录已删除"
    else
        log "保留构建目录"
    fi
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
            --jobs=*)
                JOBS="${1#*=}"
                shift
                ;;
            --help)
                echo "用法: $0 [选项]"
                echo "选项:"
                echo "  --version=VERSION    指定 Boost 版本 (默认: ${BOOST_VERSION})"
                echo "  --jobs=N             指定并行构建作业数量 (默认: 系统核心数)"
                echo "  --help               显示此帮助信息"
                exit 0
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
    log "- 并行作业: ${JOBS}"
    
    # 检查是否已经安装
    if [ -d "${INSTALL_PREFIX}" ] && [ -f "${INSTALL_PREFIX}/version.txt" ]; then
        local installed_version=$(cat "${INSTALL_PREFIX}/version.txt")
        log "检测到已安装的 Boost ${installed_version}"
        
        if [ "${installed_version}" == "${BOOST_VERSION}" ]; then
            log "已安装的版本与请求的版本相同"
            log "是否要重新构建? (y/N)"
            read -r response
            if [[ ! "$response" =~ ^([yY][eE][sS]|[yY])$ ]]; then
                log "跳过构建"
                exit 0
            fi
        else
            log "已安装的版本 (${installed_version}) 与请求的版本 (${BOOST_VERSION}) 不同"
            log "将重新构建..."
        fi
    fi
    
    # 执行构建步骤
    check_prerequisites
    download_boost
    build_boost
    finish_install
    cleanup
    
    log "Boost ${BOOST_VERSION} 构建和安装完成!"
}

# 执行主函数
main "$@"
