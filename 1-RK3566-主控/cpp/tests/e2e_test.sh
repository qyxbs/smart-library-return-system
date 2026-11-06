#!/bin/bash
# =============================================================================
# e2e_test.sh —— 端到端集成测试
# =============================================================================
# 【测什么】
#   启动真实的 book_server，用 curl 走一遍完整业务流程：
#     健康检查 → 登录 → 提权 → 录书 → 搜索 → 入库 → 借书 → 还书
#   每一步都检查返回的 code 和关键字段。
#
# 【怎么跑】
#   cd cpp/build && bash ../tests/e2e_test.sh
#
# 【为什么不写成 C++ 测试】
#   端到端测试需要启动独立进程（服务器）+ 发真实 HTTP 请求。
#   用 shell + curl 最直接，也方便手工复现某一步。
# =============================================================================
set -u

PORT=19010
DB=/tmp/e2e_$$.db
LOG=/tmp/e2e_$$.log
PASS=0
FAIL=0

cleanup() {
    if [ -n "${SERVER_PID:-}" ]; then
        kill "$SERVER_PID" 2>/dev/null
        wait "$SERVER_PID" 2>/dev/null
    fi
    rm -f "$DB" "$DB-wal" "$DB-shm" "$LOG"
}
trap cleanup EXIT

check() {
    local desc="$1"
    local cond="$2"
    if [ "$cond" = "0" ]; then
        PASS=$((PASS+1))
    else
        FAIL=$((FAIL+1))
        echo "  [FAIL] $desc"
    fi
}

# 从 JSON 里取字段（用 python3，比 grep 可靠）
jget() {
    python3 -c "
import json,sys
try:
    d=json.load(sys.stdin)
    for k in sys.argv[1].split('.'):
        d=d[k] if isinstance(d,dict) else d[int(k)]
    print(d)
except Exception:
    print('')
" "$1"
}

echo "=== 启动服务器（端口 $PORT）==="
BOOK_DB_PATH="$DB" BOOK_LOG_PATH="$LOG" BOOK_HTTP_PORT="$PORT" \
    BOOK_STATIC_DIR=../static ./book_server > /tmp/e2e_stdout_$$.log 2>&1 &
SERVER_PID=$!
sleep 2

if ! kill -0 "$SERVER_PID" 2>/dev/null; then
    echo "服务器启动失败，日志："
    cat /tmp/e2e_stdout_$$.log
    exit 1
fi
echo "服务器已启动 (PID $SERVER_PID)"
echo ""

BASE="http://127.0.0.1:$PORT"
TOKEN=""

# =============================================================================
echo "--- 1. 健康检查 ---"
R=$(curl -s "$BASE/api/health")
check "健康检查返回 200" "$(echo "$R" | jget code | grep -q 200 && echo 0 || echo 1)"
check "模式是 cpp" "$(echo "$R" | jget data.mode | grep -q cpp && echo 0 || echo 1)"

# =============================================================================
echo "--- 2. 用户登录 ---"
R=$(curl -s -X POST "$BASE/api/users/login" \
    -H "Content-Type: application/json" \
    -d '{"openid":"e2e_user","nickname":"E2E测试"}')
check "登录成功" "$(echo "$R" | jget code | grep -q 200 && echo 0 || echo 1)"
TOKEN=$(echo "$R" | jget data.token)
check "拿到 token" "$([ -n "$TOKEN" ] && echo 0 || echo 1)"
USER_ID=$(echo "$R" | jget data.user_id)

# =============================================================================
echo "--- 3. 提升为管理员 ---"
R=$(curl -s -X POST "$BASE/api/users/promote" \
    -H "Content-Type: application/json" \
    -d "{\"userId\":$USER_ID,\"pin\":\"123456\"}")
check "提权成功" "$(echo "$R" | jget code | grep -q 200 && echo 0 || echo 1)"

# =============================================================================
echo "--- 4. 录入书目 ---"
R=$(curl -s -X POST "$BASE/api/book/add" \
    -H "Content-Type: application/json" \
    -H "Authorization: Bearer $TOKEN" \
    -d '{"title":"三体","category":"文学"}')
check "录入成功" "$(echo "$R" | jget code | grep -q 200 && echo 0 || echo 1)"

R=$(curl -s "$BASE/api/book/search?keyword=%E4%B8%89%E4%BD%93")
check "搜索能搜到" "$(echo "$R" | jget code | grep -q 200 && echo 0 || echo 1)"
BOOK_ID=$(echo "$R" | python3 -c "
import json,sys
d=json.load(sys.stdin)
print(d['data'][0]['id'] if d.get('data') else '')
")
check "拿到 book_id" "$([ -n "$BOOK_ID" ] && echo 0 || echo 1)"
check "book_id 是字符串（防 JS 精度丢失）" "$(echo "$BOOK_ID" | grep -qE '^[0-9]+$' && echo 0 || echo 1)"

# =============================================================================
echo "--- 5. 扫码入库 ---"
R=$(curl -s -X POST "$BASE/api/book-instance/scan" \
    -H "Content-Type: application/json" \
    -H "Authorization: Bearer $TOKEN" \
    -d "{\"bookId\":\"$BOOK_ID\"}")
check "入库成功" "$(echo "$R" | jget code | grep -q 200 && echo 0 || echo 1)"
COORDY=$(echo "$R" | jget data.coordY)
INST_ID=$(echo "$R" | jget data.instanceId)
check "分配到文学区（15-18）" "$([ "$COORDY" -ge 15 ] && [ "$COORDY" -le 18 ] && echo 0 || echo 1)"
check "状态为在架(3)" "$(echo "$R" | jget data.status | grep -q 3 && echo 0 || echo 1)"

# =============================================================================
echo "--- 6. 借书 ---"
R=$(curl -s -X POST "$BASE/api/book-instance/borrow" \
    -H "Content-Type: application/json" \
    -H "Authorization: Bearer $TOKEN" \
    -d "{\"instanceId\":\"$INST_ID\",\"userId\":$USER_ID}")
check "借书成功" "$(echo "$R" | jget code | grep -q 200 && echo 0 || echo 1)"
check "状态变为已借出(1)" "$(echo "$R" | jget data.status | grep -q 1 && echo 0 || echo 1)"
check "槽位已释放" "$(echo "$R" | jget data.slot_id | grep -q '^0$' && echo 0 || echo 1)"
DUE=$(echo "$R" | jget data.due_date)
check "设置了应还日期" "$([ -n "$DUE" ] && echo 0 || echo 1)"

# =============================================================================
echo "--- 7. 重复借同一本应失败 ---"
R=$(curl -s -X POST "$BASE/api/book-instance/borrow" \
    -H "Content-Type: application/json" \
    -H "Authorization: Bearer $TOKEN" \
    -d "{\"instanceId\":\"$INST_ID\",\"userId\":$USER_ID}")
check "重复借书被拒绝" "$(echo "$R" | jget code | grep -qE '^4[0-9][0-9]$' && echo 0 || echo 1)"

# =============================================================================
echo "--- 8. 还书 ---"
R=$(curl -s -X POST "$BASE/api/book-instance/return" \
    -H "Content-Type: application/json" \
    -H "Authorization: Bearer $TOKEN" \
    -d "{\"instanceId\":\"$INST_ID\"}")
check "还书成功" "$(echo "$R" | jget code | grep -q 200 && echo 0 || echo 1)"
check "状态变为待上架(2)" "$(echo "$R" | jget data.status | grep -q 2 && echo 0 || echo 1)"
check "逾期天数为 0" "$(echo "$R" | jget data.overdue_days | grep -q '^0$' && echo 0 || echo 1)"

# =============================================================================
echo "--- 9. 重新上架 ---"
R=$(curl -s -X POST "$BASE/api/book-instance/re-shelve" \
    -H "Content-Type: application/json" \
    -H "Authorization: Bearer $TOKEN" \
    -d "{\"instanceId\":\"$INST_ID\"}")
check "重新上架成功" "$(echo "$R" | jget code | grep -q 200 && echo 0 || echo 1)"
check "回到文学区" "$(echo "$R" | jget data.coordY | grep -qE '^1[5-8]$' && echo 0 || echo 1)"

# =============================================================================
echo "--- 10. 无权限访问 ---"
R=$(curl -s -X POST "$BASE/api/book/add" \
    -H "Content-Type: application/json" \
    -d '{"title":"测试"}')
check "未登录录入被拒(401)" "$(echo "$R" | jget code | grep -q 401 && echo 0 || echo 1)"

# =============================================================================
echo "--- 11. 不存在的接口 ---"
R=$(curl -s "$BASE/api/not/exist")
check "返回 404" "$(echo "$R" | jget code | grep -q 404 && echo 0 || echo 1)"

# =============================================================================
echo ""
echo "========================================"
echo "通过 $PASS 项，失败 $FAIL 项"
echo "========================================"

rm -f /tmp/e2e_stdout_$$.log
[ "$FAIL" -eq 0 ] && exit 0 || exit 1
