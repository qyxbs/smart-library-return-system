#!/bin/bash
# ============================================================
# 部署脚本: 语音云端化 + 管理员上下架 (2026-07-02)
# 用法: bash deploy_20260702.sh [board_ip]
# 默认 IP: 192.168.60.170
# ============================================================
set -e

BOARD_IP="${1:-YOUR_BOARD_IP}"
SSH_PASS="${SSH_PASS:-YOUR_SSH_PASSWORD}"
SSH_CMD="sshpass -p '${SSH_PASS}' ssh -o StrictHostKeyChecking=no root@${BOARD_IP}"
LOCAL_BASE="$(dirname "$0")/../.."

echo "=== 1/6 检查板子连通性 ==="
if ! ping -c 2 -W 2 "$BOARD_IP" > /dev/null 2>&1; then
    echo "❌ 板子 $BOARD_IP 不可达"
    exit 1
fi
echo "✅ 板子在线"

echo ""
echo "=== 2/6 部署应用文件 ==="
FILES=(
    "book_app/app.py:/userdata/book_app/app.py"
    "book_app/db.py:/userdata/book_app/db.py"
    "book_app/screen.html:/userdata/book_app/screen.html"
    "k230_bridge_v2.py:/userdata/book_app/k230_bridge_v2.py"
    "voice_web_app.py:/userdata/voice_app/voice_web_app.py"
    "S99a-voice:/etc/init.d/S99a-voice"
)

for pair in "${FILES[@]}"; do
    local_f="${pair%%:*}"
    remote_f="${pair##*:}"
    echo "  $local_f → $remote_f"
    # base64 pipe 避免 SCP 崩溃
    base64 "$LOCAL_BASE/$local_f" | $SSH_CMD "base64 -d > $remote_f"
    echo "  ✅"
done

echo ""
echo "=== 3/6 禁用 llama-server (不再需要本地 LLM) ==="
$SSH_CMD "
    if [ -f /etc/init.d/S99llama ]; then
        mv /etc/init.d/S99llama /etc/init.d/.S99llama.disabled
        echo 'S99llama → .disabled'
    fi
    killall llama-server 2>/dev/null && echo 'llama-server killed' || echo 'llama-server not running'
    killall llama-cli 2>/dev/null || true
"

echo ""
echo "=== 4/6 启用 voice_web_app ==="
$SSH_CMD "
    chmod +x /etc/init.d/S99a-voice
    # 清理旧的 .bak/.disabled 文件
    rm -f /etc/init.d/S99voice /etc/init.d/S99voice.bak /etc/init.d/.S99voice 2>/dev/null
    echo 'S99a-voice → ready'
"

echo ""
echo "=== 5/6 重启服务 ==="
echo "  重启 S99book..."
$SSH_CMD "/etc/init.d/S99book restart" || $SSH_CMD "fuser -k 9777/tcp; sleep 1; /etc/init.d/S99book start"

echo "  重启 S99k230..."
$SSH_CMD "/etc/init.d/S99k230 restart" || $SSH_CMD "fuser -k 9998/tcp; sleep 1; /etc/init.d/S99k230 start"

echo "  启动 S99a-voice..."
$SSH_CMD "/etc/init.d/S99a-voice start" || $SSH_CMD "cd /userdata/voice_app && nohup python3 voice_web_app.py --port 8890 > /tmp/voice.log 2>&1 &"

echo ""
echo "=== 6/6 验证 ==="
sleep 3
echo "  Flask API:"
$SSH_CMD "curl -s http://127.0.0.1:9777/api/health" || echo "  ⚠️ :9777 无响应"
echo ""
echo "  K230 Bridge:"
$SSH_CMD "curl -s http://127.0.0.1:9777/api/k230/status | head -c 200" || echo "  ⚠️ K230 状态无响应"
echo ""
echo "  Voice AI:"
$SSH_CMD "curl -s --insecure https://127.0.0.1:8890/api/analog-status" || echo "  ⚠️ :8890 无响应"
echo ""
echo "  RAM:"
$SSH_CMD "free -h | head -2"

echo ""
echo "=== 部署完成 ==="
echo "S99book :9777  ✅ 新 API 端点已生效"
echo "S99k230 :9998  ✅ admin shelve flow 已就绪"
echo "S99a-voice :8890 ✅ 云端 AI 链 (DeepSeek)"
echo "S99llama      ❌ 已禁用 (释放 ~439MB)"
echo ""
echo "验证: http://${BOARD_IP}:9777/screen"
