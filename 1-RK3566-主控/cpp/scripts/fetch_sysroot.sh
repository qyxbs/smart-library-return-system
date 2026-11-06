#!/bin/bash
# =============================================================================
# fetch_sysroot.sh —— 从板端提取交叉编译所需的 SQLite 库
# =============================================================================
# 用法：
#   BOARD=root@192.168.1.100 bash cpp/scripts/fetch_sysroot.sh
#   （默认 BOARD=root@<BOARD_IP>，请替换为实际板子地址）
#
# 提取内容：/usr/lib/aarch64-linux-gnu/libsqlite3.so* 与 /usr/include/sqlite3.h
# 目标目录：cpp/sysroot-arm64/
# =============================================================================
set -e
BOARD="${BOARD:-root@<BOARD_IP>}"
DEST="$(cd "$(dirname "$0")/.." && pwd)/sysroot-arm64"
mkdir -p "$DEST/lib/aarch64-linux-gnu" "$DEST/include"

echo "从 $BOARD 提取 SQLite 到 $DEST ..."
scp "$BOARD:/usr/lib/aarch64-linux-gnu/libsqlite3.so"* "$DEST/lib/aarch64-linux-gnu/"
scp "$BOARD:/usr/include/sqlite3.h" "$DEST/include/" 2>/dev/null || true
echo "完成。若板端库路径不同，请手动拷贝 libsqlite3.so 到 $DEST/lib/aarch64-linux-gnu/"
