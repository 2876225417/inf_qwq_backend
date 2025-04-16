#!/bin/bash
# Clang 环境配置脚本
# 使用方法: source /path/to/clang/env.sh

# 获取脚本所在目录的绝对路径
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"

# 设置颜色输出
GREEN='\033[0;32m'
BLUE='\033[0;34m'
YELLOW='\033[0;33m'
NC='\033[0m' # No Color

# 检查是否通过 source 命令执行
if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then
    echo -e "${YELLOW}警告: 此脚本应该使用 'source' 命令执行，例如:${NC}"
    echo -e "${BLUE}source ${BASH_SOURCE[0]}${NC}"
    exit 1
fi

# 备份原始环境变量
if [ -z "$ORIGINAL_PATH" ]; then
    export ORIGINAL_PATH="$PATH"
fi

if [ -z "$ORIGINAL_LD_LIBRARY_PATH" ]; then
    export ORIGINAL_LD_LIBRARY_PATH="$LD_LIBRARY_PATH"
fi

if [ -z "$ORIGINAL_MANPATH" ]; then
    export ORIGINAL_MANPATH="$MANPATH"
fi

if [ -z "$ORIGINAL_CPATH" ]; then
    export ORIGINAL_CPATH="$CPATH"
fi

# 设置 Clang 环境变量
export PATH="${SCRIPT_DIR}/bin:$ORIGINAL_PATH"
export LD_LIBRARY_PATH="${SCRIPT_DIR}/lib:$ORIGINAL_LD_LIBRARY_PATH"
export MANPATH="${SCRIPT_DIR}/share/man:$ORIGINAL_MANPATH"
export CPATH="${SCRIPT_DIR}/include:$ORIGINAL_CPATH"

# 设置 CC 和 CXX 环境变量
export CC="${SCRIPT_DIR}/bin/clang"
export CXX="${SCRIPT_DIR}/bin/clang++"

# 检查 Clang 是否可用
if [ -x "${SCRIPT_DIR}/bin/clang" ]; then
    CLANG_VERSION=$("${SCRIPT_DIR}/bin/clang" --version | head -n 1)
    echo -e "${GREEN}已配置 Clang 环境:${NC}"
    echo -e "${BLUE}$CLANG_VERSION${NC}"
else
    echo -e "${YELLOW}警告: 未在 ${SCRIPT_DIR}/bin 目录找到可执行的 clang${NC}"
fi

# 添加便捷函数
clang_reset() {
    if [ -n "$ORIGINAL_PATH" ]; then
        export PATH="$ORIGINAL_PATH"
    fi
    
    if [ -n "$ORIGINAL_LD_LIBRARY_PATH" ]; then
        export LD_LIBRARY_PATH="$ORIGINAL_LD_LIBRARY_PATH"
    fi
    
    if [ -n "$ORIGINAL_MANPATH" ]; then
        export MANPATH="$ORIGINAL_MANPATH"
    fi
    
    if [ -n "$ORIGINAL_CPATH" ]; then
        export CPATH="$ORIGINAL_CPATH"
    fi
    
    unset CC CXX
    
    echo -e "${GREEN}已重置环境变量到原始状态${NC}"
}

# 显示帮助信息
clang_info() {
    echo -e "${BLUE}Clang 环境配置信息:${NC}"
    echo -e "${GREEN}Clang 位置:${NC} ${SCRIPT_DIR}/bin/clang"
    echo -e "${GREEN}库文件路径:${NC} ${SCRIPT_DIR}/lib"
    echo -e "${GREEN}头文件路径:${NC} ${SCRIPT_DIR}/include"
    echo -e "${GREEN}手册页路径:${NC} ${SCRIPT_DIR}/share/man"
    echo -e "${GREEN}环境变量:${NC}"
    echo -e "  CC=${CC}"
    echo -e "  CXX=${CXX}"
    echo -e "${GREEN}可用命令:${NC}"
    echo -e "  clang_info  - 显示此帮助信息"
    echo -e "  clang_reset - 重置环境变量到原始状态"
    echo
    echo -e "${GREEN}可用工具:${NC}"
    ls -1 "${SCRIPT_DIR}/bin" | grep -E 'clang|llvm' | sort | while read tool; do
        echo -e "  ${BLUE}${tool}${NC}"
    done
}

# 自动显示帮助信息
clang_info
echo -e "${GREEN}环境变量已设置。使用 'clang_reset' 可以恢复原始环境。${NC}"
