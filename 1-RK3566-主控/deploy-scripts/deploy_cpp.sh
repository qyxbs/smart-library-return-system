#!/bin/bash
# =============================================================================
# deploy_cpp.sh —— 构建并部署 C++ 主控到板端
# =============================================================================
# 用法：
#   BOARD=root@192.168.1.100 bash deploy-scripts/deploy_cpp.sh
#   （默认 BOARD=root@<BOARD_IP>）
#
# 前置：
#   1. 已安装 aarch64 交叉编译工具链
#   2. 已准备 cpp/sysroot-arm64（运行 cpp/scripts/fetch_sysroot.sh）
# =============================================================================
set -e
BOARD="${BOARD:-root@<BOARD_IP>}"
DEST=/userdata/book_app
cd "$(dirname "$0")/../cpp"

echo "=== 交叉编译 ==="
cmake -B build-arm64 -DCMAKE_TOOLCHAIN_FILE=toolchain-aarch64.cmake -DCMAKE_BUILD_TYPE=Release .
cmake --build build-arm64 -j

echo "=== 部署二进制与静态页面 ==="
ssh "$BOARD" "mkdir -p $DEST"
scp build-arm64/book_server build-arm64/k230_bridge "$BOARD:$DEST/"
scp -r static "$BOARD:$DEST/"

echo "=== 安装开机脚本并重启服务 ==="
scp ../init-scripts/S99book ../init-scripts/S99bridge "$BOARD:/etc/init.d/"
ssh "$BOARD" "chmod +x /etc/init.d/S99book /etc/init.d/S99bridge; /etc/init.d/S99book restart; /etc/init.d/S99bridge restart"

echo "=== 完成 ==="
ssh "$BOARD" "ps | grep -E 'book_server|k230_bridge' | grep -v grep"
