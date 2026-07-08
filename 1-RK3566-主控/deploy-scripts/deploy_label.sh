#!/bin/bash
# ============================================================
# 部署脚本: 新标签纸打印适配 (2026-07-06)
# 50mm×50mm 替代旧 40×30mm
# 用法: bash deploy_label.sh [board_ip]
# ============================================================
set -e

BOARD_IP="${1:-YOUR_BOARD_IP}"
SSH_PASS="${SSH_PASS:-YOUR_SSH_PASSWORD}"
SSH="sshpass -p '${SSH_PASS}' ssh -o StrictHostKeyChecking=no root@${BOARD_IP}"
LOCAL="$(dirname "$0")/.."

echo "=== EM20L 新标签纸 50×50mm 部署 ==="
echo "目标: ${BOARD_IP}"
echo ""

# 1. 检查连通性
echo "=== 1/4 检查连通性 ==="
if ! ping -c 2 -W 3 "$BOARD_IP" > /dev/null 2>&1; then
    echo "❌ 板子 $BOARD_IP 不可达"
    echo "请确认板子 IP 地址是否正确"
    exit 1
fi
echo "✅ 板子在线"

# 2. 部署 print_label.py
echo ""
echo "=== 2/4 部署 print_label.py ==="
base64 "$LOCAL/print_label.py" | $SSH "base64 -d > /userdata/book_app/print_label.py"
$SSH "chmod +x /userdata/book_app/print_label.py"
echo "✅ print_label.py → /userdata/book_app/"

# 3. 部署 app.py (更新了 print_label 函数调用路径)
echo ""
echo "=== 3/4 部署 app.py ==="
base64 "$LOCAL/app.py" | $SSH "base64 -d > /userdata/book_app/app.py"
echo "✅ app.py → /userdata/book_app/"

# 4. 测试打印
echo ""
echo "=== 4/4 测试打印 ==="
echo "  发送测试标签..."
$SSH "python3 /userdata/book_app/print_label.py 0 '测试标签 50x50'"
echo ""

# 重启 Flask
echo "  重启 S99book..."
$SSH "/etc/init.d/S99book restart" || $SSH "fuser -k 9777/tcp; sleep 1; cd /userdata/book_app && nohup python3 app.py --port 9777 > /tmp/book.log 2>&1 &"
sleep 2

echo ""
echo "=== 部署完成 ==="
echo "新标签: 50mm×50mm, 400×400 dots @ 203DPI"
echo "QR: 居中, W=7, 175×175 dots"
echo "文字: X=30, 书名 Y=225(24pt) + ID Y=275(18pt)"
echo ""
echo "验证方式:"
echo "  1. 手动打印: python3 /userdata/book_app/print_label.py <book_id> '书名'"
echo "  2. API 测试: curl -X POST http://${BOARD_IP}:9777/api/printer/test -H 'Authorization: Bearer <token>'"
echo "  3. 管理员面板: http://${BOARD_IP}:9777/screen → 系统工具 → 打印测试"
echo ""
echo "调试模式: LABEL_DEBUG=1 python3 /userdata/book_app/print_label.py 0 '测试'"
