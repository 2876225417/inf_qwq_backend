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
BUILD_SHARED="yes"  # 是否构建共享库
BUILD_STATIC="yes"  # 是否构建静态库
BUILD_TYPE="release"  # 构建类型: release, debug, profile
COMPONENTS=""  # 指定要构建的组件，空表示全部构建

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

# 检查系统资源并调整并行作业数
check_resources() {
    # 检查可用内存
    local mem_gb=0
    
    if command -v free &> /dev/null; then
        mem_gb=$(free -g | awk '/^Mem:/{print $2}')
    elif [ "$(uname)" == "Darwin" ] && command -v sysctl &> /dev/null; then
        # macOS 下获取内存大小（以GB为单位）
        mem_gb=$(($(sysctl -n hw.memsize) / 1024 / 1024 / 1024))
    fi
    
    # 如果内存小于4GB，减少并行作业数
    if [ "$mem_gb" -lt 4 ] && [ "$JOBS" -gt 2 ]; then
        local old_jobs=$JOBS
        JOBS=2
        warn "系统内存较小 (${mem_gb}GB)，已将并行作业数从 $old_jobs 降低到 $JOBS"
    fi
    
    # 如果内存充足但作业数过多，适当调整
    if [ "$mem_gb" -gt 0 ] && [ "$JOBS" -gt "$((mem_gb * 3))" ]; then
        local old_jobs=$JOBS
        JOBS=$((mem_gb * 3))
        info "根据系统内存 (${mem_gb}GB) 调整并行作业数从 $old_jobs 到 $JOBS"
    fi
}

# 检查必要工具
check_prerequisites() {
    log "检查必要工具..."
    
    local missing_tools=()
    
    # 检查必要工具
    for tool in tar make; do
        if ! command -v $tool &> /dev/null; then
            missing_tools+=($tool)
        fi
    done
    
    # 检查下载工具
    local has_download_tool=false
    for tool in wget curl; do
        if command -v $tool &> /dev/null; then
            has_download_tool=true
            break
        fi
    done
    
    if [ "$has_download_tool" = false ]; then
        missing_tools+=("wget or curl")
    fi
    
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
    
    # 检查系统资源并调整并行作业数
    check_resources
    
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
    
    # 验证下载的文件
    if [ ! -f "${tar_file}" ] || [ ! -s "${tar_file}" ]; then
        error "下载的文件无效或为空"
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
        "variant=${BUILD_TYPE}"
        "--layout=tagged"
    )
    
    # 添加链接类型选项
    local link_options=()
    if [ "${BUILD_SHARED}" = "yes" ] && [ "${BUILD_STATIC}" = "yes" ]; then
        link_options+=("link=shared,static")
    elif [ "${BUILD_SHARED}" = "yes" ]; then
        link_options+=("link=shared")
    elif [ "${BUILD_STATIC}" = "yes" ]; then
        link_options+=("link=static")
    else
        error "至少需要构建静态库或共享库"
    fi
    b2_options+=("${link_options[@]}")
    
    # 运行时链接选项
    if [ "${BUILD_SHARED}" = "yes" ]; then
        b2_options+=("runtime-link=shared")
    else
        b2_options+=("runtime-link=static")
    fi
    
    # 检查是否使用 Clang
    if [[ "$CXX" == *"clang++"* ]]; then
        b2_options+=("toolset=clang")
        log "使用 Clang 工具链构建 Boost"
    elif [[ "$CXX" == *"g++"* ]]; then
        b2_options+=("toolset=gcc")
        log "使用 GCC 工具链构建 Boost"
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
    
    # 记录开始时间
    local start_time=$(date +%s)
    
    # 构建和安装
    log "开始构建 Boost (使用 $JOBS 个并行作业)..."
    info "构建可能需要一些时间，请耐心等待..."
    info "构建选项: ${b2_options[*]}"
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
    
    # 记录构建配置
    cat > "${INSTALL_PREFIX}/build_config.txt" << EOF
Boost 版本: ${BOOST_VERSION}
构建日期: $(date '+%Y-%m-%d %H:%M:%S')
构建类型: ${BUILD_TYPE}
共享库: ${BUILD_SHARED}
静态库: ${BUILD_STATIC}
编译器: ${CXX}
组件: ${COMPONENTS:-全部}
EOF
    
    # 显示安装的库
    log "已安装的 Boost 库:"
    local lib_pattern="libboost_*.so"
    if [ "${BUILD_STATIC}" = "yes" ] && [ "${BUILD_SHARED}" != "yes" ]; then
        lib_pattern="libboost_*.a"
    fi
    
    find "${INSTALL_PREFIX}/lib" -name "${lib_pattern}" | sed 's/.*libboost_\(.*\)\..*/\1/' | sort | uniq | tr '\n' ', '
    echo ""
    
    # 创建环境变量设置脚本
    cat > "${INSTALL_PREFIX}/env.sh" << EOF
#!/bin/bash
# Boost 环境配置脚本
# 使用方法: source ${INSTALL_PREFIX}/env.sh

export BOOST_ROOT="${INSTALL_PREFIX}"
export CPATH="\${BOOST_ROOT}/include:\${CPATH}"
export LIBRARY_PATH="\${BOOST_ROOT}/lib:\${LIBRARY_PATH}"
export LD_LIBRARY_PATH="\${BOOST_ROOT}/lib:\${LD_LIBRARY_PATH}"

echo "Boost ${BOOST_VERSION} 环境已设置"
EOF
    chmod +x "${INSTALL_PREFIX}/env.sh"
    
    log "要在 CMake 项目中使用 Boost，可以添加以下内容到 CMakeLists.txt:"
    echo "  set(BOOST_ROOT \"${INSTALL_PREFIX}\")"
    echo "  find_package(Boost REQUIRED COMPONENTS system filesystem ...)"
    echo "  target_link_libraries(your_target PRIVATE \${Boost_LIBRARIES})"
    log "或者使用环境变量脚本: source ${INSTALL_PREFIX}/env.sh"
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

# 帮助函数
show_help() {
    echo "Boost 构建脚本"
    echo "用法: $0 [选项]"
    echo "选项:"
    echo "  --version=VERSION    指定 Boost 版本 (默认: ${BOOST_VERSION})"
    echo "  --prefix=PATH        指定安装路径 (默认: ${INSTALL_PREFIX})"
    echo "  --jobs=N             指定并行构建作业数量 (默认: 系统核心数)"
    echo "  --shared=yes|no      是否构建共享库 (默认: yes)"
    echo "  --static=yes|no      是否构建静态库 (默认: yes)"
    echo "  --type=TYPE          构建类型: release, debug, profile (默认: release)"
    echo "  --components=LIST    指定要构建的组件，逗号分隔 (默认: 全部)"
    echo "  --help               显示此帮助信息"
    echo
    echo "示例:"
    echo "  $0 --version=1.87.0 --shared=yes --static=no --components=system,filesystem,thread"
    exit 0
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
            --help)
                show_help
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
