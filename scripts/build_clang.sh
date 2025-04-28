#!/usr/bin/env bash
set -euo pipefail

# 配置参数
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_TYPE="Release"    # 构建类型：Debug, Release, RelWithDebInfo, MinSizeRel
INSTALL_PREFIX="${SCRIPT_DIR}/clang"  # 安装目录
BUILD_DIR="${SCRIPT_DIR}/llvm-build"  # 构建目录
SOURCE_DIR="${SCRIPT_DIR}/llvm-project"  # 源码目录

# 默认并行构建数量
if command -v nproc &> /dev/null; then
    JOBS=$(nproc)
elif [ "$(uname)" == "Darwin" ] && command -v sysctl &> /dev/null; then
    JOBS=$(sysctl -n hw.ncpu)
else
    JOBS=4
fi

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

# 检测当前系统架构
detect_architecture() {
    local arch=$(uname -m)
    case "$arch" in
        x86_64)
            LLVM_TARGETS="X86"
            ;;
        aarch64|arm64)
            LLVM_TARGETS="AArch64"
            ;;
        arm*)
            LLVM_TARGETS="ARM"
            ;;
        *)
            LLVM_TARGETS="X86;ARM;AArch64"
            warn "未能识别的架构: $arch，将构建多架构支持"
            ;;
    esac
    info "检测到架构: $arch，将构建支持: $LLVM_TARGETS"
}

# 检查可用内存并调整并行作业数
check_memory_and_adjust_jobs() {
    local mem_gb=0
    
    if command -v free &> /dev/null; then
        mem_gb=$(free -g | awk '/^Mem:/{print $2}')
    elif [ "$(uname)" == "Darwin" ] && command -v sysctl &> /dev/null; then
        # macOS 下获取内存大小（以GB为单位）
        mem_gb=$(($(sysctl -n hw.memsize) / 1024 / 1024 / 1024))
    fi
    
    # 如果内存小于8GB，减少并行作业数
    if [ "$mem_gb" -lt 8 ] && [ "$JOBS" -gt 2 ]; then
        local old_jobs=$JOBS
        JOBS=2
        warn "系统内存较小 (${mem_gb}GB)，已将并行作业数从 $old_jobs 降低到 $JOBS"
    fi
    
    # 如果内存较大，但作业数过多，适当调整
    if [ "$mem_gb" -gt 0 ] && [ "$JOBS" -gt "$((mem_gb * 2))" ]; then
        local old_jobs=$JOBS
        JOBS=$((mem_gb * 2))
        info "根据系统内存 (${mem_gb}GB) 调整并行作业数从 $old_jobs 到 $JOBS"
    fi
}

# 检查必要工具并设置编译器
check_prerequisites() {
    log "检查必要工具..."
    
    local missing_tools=()
    
    # 检查必要工具
    for tool in git cmake python3 make; do
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
        local clang_version=$(clang --version | head -n 1)
        log "找到 Clang: $clang_version"
        export CC=clang
        export CXX=clang++
        log "将使用 Clang 作为编译器"
    elif command -v gcc &> /dev/null && command -v g++ &> /dev/null; then
        local gcc_version=$(gcc --version | head -n 1)
        log "找到 GCC: $gcc_version"
        export CC=gcc
        export CXX=g++
        log "将使用 GCC 作为编译器"
    else
        error "未找到可用的 C/C++ 编译器"
    fi
    
    # 检查系统资源
    log "检查系统资源..."
    
    # 检查可用内存并调整并行作业数
    check_memory_and_adjust_jobs
    
    # 检查可用磁盘空间
    local free_space_gb=0
    if command -v df &> /dev/null; then
        free_space_gb=$(df -BG "$SCRIPT_DIR" | awk 'NR==2 {gsub("G", "", $4); print $4}')
        if [ "$free_space_gb" -lt 20 ]; then
            warn "可用磁盘空间小于 20GB (检测到 ${free_space_gb}GB)，可能不足以完成构建"
        else
            info "可用磁盘空间: ${free_space_gb}GB"
        fi
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
    
    # 确定构建项目
    local projects="clang;clang-tools-extra;lld;lldb"
    local runtimes="compiler-rt;libcxx;libcxxabi;libunwind;openmp"
    
    # 构建核心组件
    cmake $USE_NINJA -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
          -DCMAKE_INSTALL_PREFIX="$INSTALL_PREFIX" \
          -DLLVM_ENABLE_PROJECTS="$projects" \
          -DLLVM_ENABLE_RUNTIMES="$runtimes" \
          -DLLVM_TARGETS_TO_BUILD="$LLVM_TARGETS" \
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
          -DLLVM_PARALLEL_LINK_JOBS="$((JOBS / 2 > 0 ? JOBS / 2 : 1))" \
          -DLLVM_OPTIMIZED_TABLEGEN=ON \
          -DLLVM_USE_SPLIT_DWARF=ON \
          -DLLVM_ENABLE_LTO=OFF \
          "$SOURCE_DIR/llvm" || error "CMake 配置失败"
          
    log "配置完成"
}

# 构建 LLVM
build_llvm() {
    log "开始构建 LLVM (使用 $JOBS 个并行作业)..."
    cd "$BUILD_DIR"
    
    # 显示估计的构建时间
    info "构建可能需要较长时间，请耐心等待..."
    
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
    
    # 创建环境变量设置脚本
    cat > "${INSTALL_PREFIX}/env.sh" << EOF
#!/bin/bash
export PATH="${INSTALL_PREFIX}/bin:\$PATH"
export LD_LIBRARY_PATH="${INSTALL_PREFIX}/lib:\$LD_LIBRARY_PATH"
export CPATH="${INSTALL_PREFIX}/include:\$CPATH"
export MANPATH="${INSTALL_PREFIX}/share/man:\$MANPATH"
EOF
    chmod +x "${INSTALL_PREFIX}/env.sh"
    
    log "要使用新安装的 Clang，请运行:"
    log "  source \"${INSTALL_PREFIX}/env.sh\""
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
            --prefix=*)
                INSTALL_PREFIX="${1#*=}"
                shift
                ;;
            --help)
                echo "用法: $0 [选项]"
                echo "选项:"
                echo "  --build-type=TYPE    指定构建类型: Debug, Release, RelWithDebInfo, MinSizeRel (默认: $BUILD_TYPE)"
                echo "  --jobs=N             指定并行构建作业数量 (默认: 自动检测)"
                echo "  --prefix=PATH        指定安装路径 (默认: $INSTALL_PREFIX)"
                echo "  --help               显示此帮助信息"
                exit 0
                ;;
            *)
                error "未知选项: $1"
                ;;
        esac
    done
    
    # 检测当前系统架构
    detect_architecture
    
    log "配置摘要:"
    log "- 构建类型: ${BUILD_TYPE}"
    log "- 安装路径: ${INSTALL_PREFIX}"
    log "- 构建目录: ${BUILD_DIR}"
    log "- 源码目录: ${SOURCE_DIR}"
    log "- 目标架构: ${LLVM_TARGETS}"
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
