#!/usr/bin/env bash
set -euo pipefail

# 配置参数
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_TYPE="Release"    # 构建类型：Debug, Release, RelWithDebInfo, MinSizeRel
INSTALL_PREFIX="${SCRIPT_DIR}/clang"  # 安装目录
BUILD_DIR="${SCRIPT_DIR}/llvm-build"  # 构建目录
SOURCE_DIR="${SCRIPT_DIR}/llvm-project"  # 源码目录
#JOBS=$(nproc || sysctl -n hw.ncpu || echo 4)  # 并行构建数量

JOBS=8

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

# 检查必要工具并设置编译器
check_prerequisites() {
    log "检查必要工具..."
    
    local missing_tools=()
    
    # 检查必要工具
    for tool in git cmake python3 tar make; do
        if ! command -v $tool &> /dev/null; then
            missing_tools+=($tool)
        fi
    done
    
    if [ ${#missing_tools[@]} -ne 0 ]; then
        error "缺少必要工具: ${missing_tools[*]}\n请先安装这些工具后再运行此脚本"
    fi
    
    # 检查 Ninja（可选但推荐）
    if command -v ninja &> /dev/null; then
        USE_NINJA="-G Ninja"
        log "找到 Ninja，将使用 Ninja 构建系统"
    else
        USE_NINJA=""
        warn "未找到 Ninja，将使用默认构建系统。建议安装 Ninja 以加速构建"
    fi
    
    # 检查编译器并设置优先级
    if command -v clang &> /dev/null && command -v clang++ &> /dev/null; then
        log "找到 Clang: $(clang --version | head -n 1)"
        export CC=clang
        export CXX=clang++
        log "将使用 Clang 作为编译器"
    elif command -v gcc &> /dev/null && command -v g++ &> /dev/null; then
        log "找到 GCC: $(gcc --version | head -n 1)"
        export CC=gcc
        export CXX=g++
        log "将使用 GCC 作为编译器"
    else
        error "未找到可用的 C/C++ 编译器"
    fi
    
    # 检查系统资源
    log "检查系统资源..."
    
    # 检查可用内存
    if command -v free &> /dev/null; then
        local mem_gb=$(free -g | awk '/^Mem:/{print $2}')
        if [ "$mem_gb" -lt 8 ]; then
            warn "系统内存小于 8GB (检测到 ${mem_gb}GB)，构建可能会失败或非常慢"
        else
            info "系统内存: ${mem_gb}GB"
        fi
    fi
    
    # 检查可用磁盘空间
    local free_space_gb=$(df -BG "$SCRIPT_DIR" | awk 'NR==2 {gsub("G", "", $4); print $4}')
    if [ "$free_space_gb" -lt 20 ]; then
        warn "可用磁盘空间小于 20GB (检测到 ${free_space_gb}GB)，可能不足以完成构建"
    else
        info "可用磁盘空间: ${free_space_gb}GB"
    fi
    
    # 显示并行作业数
    info "将使用 $JOBS 个并行作业进行构建"
}

# 下载 LLVM 源码
download_llvm() {
    log "下载 LLVM 项目源码..."
    
    if [ -d "$SOURCE_DIR" ]; then
        log "源码目录已存在，更新到最新版本..."
        cd "$SOURCE_DIR"
        git fetch origin
        git reset --hard origin/main
        git clean -fdx
    else
        log "克隆 LLVM 仓库..."
        git clone --depth=1 https://github.com/llvm/llvm-project.git "$SOURCE_DIR"
    fi
    
    cd "$SOURCE_DIR"
    local current_commit=$(git rev-parse HEAD)
    local current_date=$(git log -1 --format=%cd --date=short)
    log "当前使用的 LLVM 版本:"
    log "- 提交: $current_commit"
    log "- 日期: $current_date"
}

# 配置构建
configure_build() {
    log "配置 LLVM 构建..."
    
    # 创建构建目录
    mkdir -p "$BUILD_DIR"
    cd "$BUILD_DIR"
    
    # 清理旧的构建文件
    rm -f CMakeCache.txt
    
    # 构建所有主要组件
    cmake $USE_NINJA -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
          -DCMAKE_INSTALL_PREFIX="$INSTALL_PREFIX" \
          -DLLVM_ENABLE_PROJECTS="clang;clang-tools-extra;lld;lldb;mlir;flang;polly;bolt" \
          -DLLVM_ENABLE_RUNTIMES="compiler-rt;libcxx;libcxxabi;libunwind;openmp;pstl" \
          -DLLVM_TARGETS_TO_BUILD="X86;ARM;AArch64" \
          -DLLVM_ENABLE_ASSERTIONS=OFF \
          -DLLVM_BUILD_EXAMPLES=OFF \
          -DLLVM_INCLUDE_EXAMPLES=OFF \
          -DLLVM_BUILD_TESTS=OFF \
          -DLLVM_INCLUDE_TESTS=OFF \
          -DLLVM_ENABLE_DOXYGEN=OFF \
          -DLLVM_ENABLE_SPHINX=OFF \
          -DLLVM_ENABLE_OCAMLDOC=OFF \
          -DLLVM_ENABLE_ZLIB=ON \
          -DLLVM_ENABLE_ZSTD=ON \
          -DLLVM_ENABLE_LIBXML2=ON \
          -DLLVM_ENABLE_TERMINFO=ON \
          -DLLVM_ENABLE_LIBEDIT=ON \
          -DLLVM_PARALLEL_COMPILE_JOBS="$JOBS" \
          -DLLVM_PARALLEL_LINK_JOBS="$JOBS" \
          "$SOURCE_DIR/llvm" || error "CMake 配置失败"
          
    log "配置完成"
}

# 构建 LLVM
build_llvm() {
    log "开始构建 LLVM (使用 $JOBS 个并行作业)..."
    cd "$BUILD_DIR"
    
    # 显示估计的构建时间
    info "构建可能需要几个小时，请耐心等待..."
    
    # 记录开始时间
    local start_time=$(date +%s)
    
    # 构建
    cmake --build . --config "$BUILD_TYPE" -j "$JOBS" || error "构建失败"
    
    # 计算构建时间
    local end_time=$(date +%s)
    local duration=$((end_time - start_time))
    local hours=$((duration / 3600))
    local minutes=$(( (duration % 3600) / 60 ))
    local seconds=$((duration % 60))
    
    log "构建完成，耗时: ${hours}小时 ${minutes}分钟 ${seconds}秒"
}

# 安装 LLVM
install_llvm() {
    log "安装 LLVM 到 $INSTALL_PREFIX..."
    cd "$BUILD_DIR"
    
    # 确保安装目录存在
    mkdir -p "$INSTALL_PREFIX"
    
    cmake --install . || error "安装失败"
    
    log "安装完成"
    log "LLVM 已安装到: $INSTALL_PREFIX"
    
    # 显示版本信息
    if [ -f "${INSTALL_PREFIX}/bin/clang" ]; then
        log "安装的 Clang 版本:"
        "${INSTALL_PREFIX}/bin/clang" --version | head -n 1
    fi
    
    log "要使用新安装的 Clang，请将以下路径添加到环境变量中:"
    log "  PATH=\"${INSTALL_PREFIX}/bin:\$PATH\""
    log "  LD_LIBRARY_PATH=\"${INSTALL_PREFIX}/lib:\$LD_LIBRARY_PATH\""
}

# 清理函数
cleanup() {
    log "是否要删除构建目录以节省磁盘空间? (y/N)"
    read -r response
    if [[ "$response" =~ ^([yY][eE][sS]|[yY])$ ]]; then
        log "删除构建目录..."
        rm -rf "$BUILD_DIR"
        log "构建目录已删除"
    else
        log "保留构建目录"
    fi
}

# 主函数
main() {
    log "开始构建最新版本的 LLVM/Clang..."
    
    # 解析命令行参数
    while [[ $# -gt 0 ]]; do
        case $1 in
            --build-type=*)
                BUILD_TYPE="${1#*=}"
                shift
                ;;
            --jobs=*)
                JOBS="${1#*=}"
                shift
                ;;
            --help)
                echo "用法: $0 [选项]"
                echo "选项:"
                echo "  --build-type=TYPE    指定构建类型: Debug, Release, RelWithDebInfo, MinSizeRel (默认: $BUILD_TYPE)"
                echo "  --jobs=N             指定并行构建作业数量 (默认: 系统核心数)"
                echo "  --help               显示此帮助信息"
                exit 0
                ;;
            *)
                error "未知选项: $1"
                ;;
        esac
    done
    
    log "配置摘要:"
    log "- 构建类型: ${BUILD_TYPE}"
    log "- 安装路径: ${INSTALL_PREFIX}"
    log "- 构建目录: ${BUILD_DIR}"
    log "- 源码目录: ${SOURCE_DIR}"
    log "- 并行作业: ${JOBS}"
    
    # 检查是否已经安装
    if [ -d "$INSTALL_PREFIX" ] && [ -f "${INSTALL_PREFIX}/bin/clang" ]; then
        log "检测到 Clang 已安装在 ${INSTALL_PREFIX}"
        log "是否要重新构建? (y/N)"
        read -r response
        if [[ ! "$response" =~ ^([yY][eE][sS]|[yY])$ ]]; then
            log "取消构建"
            exit 0
        fi
    fi
    
    # 执行构建步骤
    check_prerequisites
    download_llvm
    configure_build
    build_llvm
    install_llvm
    cleanup
    
    log "LLVM/Clang 构建和安装完成!"
}

# 执行主函数
main "$@"
