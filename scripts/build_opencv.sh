#!/usr/bin/env bash
set -euo pipefail

# OpenCV 构建脚本
# 用途: 下载、构建并安装 OpenCV 到 3rdparty/opencv 目录

# 配置参数
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
OPENCV_VERSION="4.11.0"  # 可以根据需要更改版本
INSTALL_PREFIX="${SCRIPT_DIR}/3rdparty/opencv"
BUILD_DIR="${SCRIPT_DIR}/opencv-build"
SOURCE_DIR="${BUILD_DIR}/source"
JOBS=$(nproc || sysctl -n hw.ncpu || echo 4)  # 并行构建数量
BUILD_TYPE="Release"  # 构建类型: Release, Debug, RelWithDebInfo
WITH_CONTRIB="no"     # 是否包含 contrib 模块
ENABLE_CUDA="no"      # 是否启用 CUDA 支持
ENABLE_PYTHON="no"    # 是否构建 Python 绑定

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
    
    # 如果内存小于8GB，减少并行作业数
    if [ "$mem_gb" -lt 8 ] && [ "$JOBS" -gt 2 ]; then
        local old_jobs=$JOBS
        JOBS=2
        warn "系统内存较小 (${mem_gb}GB)，已将并行作业数从 $old_jobs 降低到 $JOBS"
    fi
    
    # 如果内存充足但作业数过多，适当调整
    if [ "$mem_gb" -gt 0 ] && [ "$JOBS" -gt "$((mem_gb * 2))" ]; then
        local old_jobs=$JOBS
        JOBS=$((mem_gb * 2))
        info "根据系统内存 (${mem_gb}GB) 调整并行作业数从 $old_jobs 到 $JOBS"
    fi
}

# 检查必要工具
check_prerequisites() {
    log "检查必要工具..."
    
    local missing_tools=()
    
    # 检查必要工具
    for tool in cmake tar make; do
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
    
    # 检查 CUDA
    if [ "${ENABLE_CUDA}" = "yes" ]; then
        if ! command -v nvcc &> /dev/null; then
            warn "未找到 CUDA 工具链 (nvcc)，将禁用 CUDA 支持"
            ENABLE_CUDA="no"
        else
            log "找到 CUDA: $(nvcc --version | head -n 1)"
        fi
    fi
    
    # 检查 Python
    if [ "${ENABLE_PYTHON}" = "yes" ]; then
        if ! command -v python3 &> /dev/null; then
            warn "未找到 Python 3，将禁用 Python 绑定"
            ENABLE_PYTHON="no"
        else
            log "找到 Python: $(python3 --version)"
        fi
    fi
    
    # 检查系统资源并调整并行作业数
    check_resources
    
    # 显示并行作业数
    info "将使用 $JOBS 个并行作业进行构建"
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
        "-DCMAKE_CXX_STANDARD=17"
    )
    
    # 如果使用 Clang，添加相应选项
    if [[ "$CXX" == *"clang++"* ]]; then
        cmake_options+=(
            "-DCMAKE_C_COMPILER=${CC}"
            "-DCMAKE_CXX_COMPILER=${CXX}"
        )
    fi
    
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
    
    # 记录开始时间
    local start_time=$(date +%s)
    
    # 配置
    log "配置 OpenCV..."
    cmake "${SOURCE_DIR}/opencv" "${cmake_options[@]}" || error "CMake 配置失败"
    
    # 构建
    log "开始构建 OpenCV (使用 $JOBS 个并行作业)..."
    info "构建可能需要较长时间，请耐心等待..."
    cmake --build . -j "${JOBS}" || error "构建失败"
    
    # 安装
    log "安装 OpenCV..."
    cmake --install . || error "安装失败"
    
    # 计算构建时间
    local end_time=$(date +%s)
    local duration=$((end_time - start_time))
    local minutes=$((duration / 60))
    local seconds=$((duration % 60))
    
    log "OpenCV ${OPENCV_VERSION} 构建完成，耗时: ${minutes}分钟 ${seconds}秒"
}

# 安装完成后的处理
finish_install() {
    log "OpenCV 已安装到 ${INSTALL_PREFIX}"
    
    # 创建版本文件，便于后续检查
    echo "${OPENCV_VERSION}" > "${INSTALL_PREFIX}/version.txt"
    
    # 记录构建配置
    cat > "${INSTALL_PREFIX}/build_config.txt" << EOF
OpenCV 版本: ${OPENCV_VERSION}
构建日期: $(date '+%Y-%m-%d %H:%M:%S')
构建类型: ${BUILD_TYPE}
包含 Contrib: ${WITH_CONTRIB}
启用 CUDA: ${ENABLE_CUDA}
启用 Python: ${ENABLE_PYTHON}
编译器: ${CXX}
EOF
    
    # 创建环境变量设置脚本
    cat > "${INSTALL_PREFIX}/env.sh" << EOF
#!/bin/bash
# OpenCV 环境配置脚本
# 使用方法: source ${INSTALL_PREFIX}/env.sh

export OpenCV_DIR="${INSTALL_PREFIX}"
export PATH="\${OpenCV_DIR}/bin:\${PATH}"
export LD_LIBRARY_PATH="\${OpenCV_DIR}/lib:\${LD_LIBRARY_PATH}"
export CPATH="\${OpenCV_DIR}/include:\${CPATH}"
export PKG_CONFIG_PATH="\${OpenCV_DIR}/lib/pkgconfig:\${PKG_CONFIG_PATH}"

echo "OpenCV ${OPENCV_VERSION} 环境已设置"
EOF
    chmod +x "${INSTALL_PREFIX}/env.sh"
    
    # 显示安装的库
    log "已安装的 OpenCV 库:"
    find "${INSTALL_PREFIX}/lib" -name "libopencv_*.so*" | sed 's/.*libopencv_\(.*\)\.so.*/\1/' | sort | uniq | tr '\n' ', '
    echo ""
    
    log "要在 CMake 项目中使用 OpenCV，可以添加以下内容到 CMakeLists.txt:"
    echo "  set(OpenCV_DIR \"${INSTALL_PREFIX}/lib/cmake/opencv4\")"
    echo "  find_package(OpenCV REQUIRED)"
    echo "  target_link_libraries(your_target PRIVATE \${OpenCV_LIBS})"
    log "或者使用环境变量脚本: source ${INSTALL_PREFIX}/env.sh"
}

# 清理函数
cleanup() {
    log "是否要删除构建目录以节省磁盘空间? (y/N)"
    read -r response
    if [[ "$response" =~ ^([yY][eE][sS]|[yY])$ ]]; then
        log "删除构建目录..."
        rm -rf "${BUILD_DIR}/build"
        log "构建目录已删除"
    else
        log "保留构建目录"
    fi
}

# 帮助函数
show_help() {
    echo "OpenCV 构建脚本"
    echo "用法: $0 [选项]"
    echo "选项:"
    echo "  --version=VERSION    指定 OpenCV 版本 (默认: ${OPENCV_VERSION})"
    echo "  --prefix=PATH        指定安装路径 (默认: ${INSTALL_PREFIX})"
    echo "  --jobs=N             指定并行构建作业数量 (默认: 系统核心数)"
    echo "  --build-type=TYPE    构建类型: Release, Debug, RelWithDebInfo (默认: ${BUILD_TYPE})"
    echo "  --with-contrib=yes|no 是否包含 contrib 模块 (默认: ${WITH_CONTRIB})"
    echo "  --enable-cuda=yes|no 是否启用 CUDA 支持 (默认: ${ENABLE_CUDA})"
    echo "  --enable-python=yes|no 是否构建 Python 绑定 (默认: ${ENABLE_PYTHON})"
    echo "  --help               显示此帮助信息"
    echo
    echo "示例:"
    echo "  $0 --version=4.9.0 --with-contrib=yes --enable-cuda=yes"
    exit 0
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
    log "- OpenCV 版本: ${OPENCV_VERSION}"
    log "- 安装路径: ${INSTALL_PREFIX}"
    log "- 构建目录: ${BUILD_DIR}"
    log "- 构建类型: ${BUILD_TYPE}"
    log "- 包含 Contrib: ${WITH_CONTRIB}"
    log "- 启用 CUDA: ${ENABLE_CUDA}"
    log "- 启用 Python: ${ENABLE_PYTHON}"
    log "- 并行作业: ${JOBS}"
    
    # 检查是否已经安装
    if [ -d "${INSTALL_PREFIX}" ] && [ -f "${INSTALL_PREFIX}/version.txt" ]; then
        local installed_version=$(cat "${INSTALL_PREFIX}/version.txt")
        log "检测到已安装的 OpenCV ${installed_version}"
        
        if [ "${installed_version}" == "${OPENCV_VERSION}" ]; then
            log "已安装的版本与请求的版本相同"
            log "是否要重新构建? (y/N)"
            read -r response
            if [[ ! "$response" =~ ^([yY][eE][sS]|[yY])$ ]]; then
                log "跳过构建"
                exit 0
            fi
        else
            log "已安装的版本 (${installed_version}) 与请求的版本 (${OPENCV_VERSION}) 不同"
            log "将重新构建..."
        fi
    fi
    
    # 执行构建步骤
    check_prerequisites
    download_opencv
    build_opencv
    finish_install
    cleanup
    
    log "OpenCV ${OPENCV_VERSION} 构建和安装完成!"
}

# 执行主函数
main "$@"
