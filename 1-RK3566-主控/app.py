#!/usr/bin/env python3
"""
智能书架 - 全本地统一 API 服务
全本地: 书本管理 + 槽位分配 + 直接 UART 控制 + WebSocket
部署: /userdata/book_app/app.py, 端口 9777
"""
import os, sys, json, time, threading
from flask import Flask, request, jsonify, send_from_directory
from flask_socketio import SocketIO, emit
from flask_cors import CORS

# Add parent dir for db import
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from db import init_db, now_str, InstanceStatus, UserRole
from db import (
    book_create, book_get, book_update, book_delete, book_list, book_search,
    instance_create, instance_get, instance_update, instance_delete, instance_page,
    instance_shelf_list, instance_borrowed_list, instance_get_by_slot,
    slot_list, slot_get, slot_update, slot_release,
    scan_allocate,
    borrow_instance, return_instance, re_shelve_instance,
    user_create, user_get, user_get_by_openid, user_list, user_update,
    log_create, log_get, log_page, log_by_instance,
)
import tasks as task_module

# ==================== UART + Print ====================
import struct, subprocess

UART4_DEV = "/dev/ttyS4"   # 9600 — STM32 坐标
UART5_DEV = "/dev/ttyS5"   # 38400 — STM32 控制

WS_CLIENTS = set()


def send_uart4(prefix_byte, coord_byte):
    """Send coordinate command to STM32 via UART4. Returns True on success."""
    try:
        with open(UART4_DEV, "wb") as f:
            f.write(struct.pack("BB", prefix_byte, coord_byte))
            f.flush()
        print(f"[UART4] {prefix_byte:#X} {coord_byte}", flush=True)
        return True
    except Exception as e:
        print(f"[UART4] Error: {e}", flush=True)
        return False


def send_uart5_control():
    """Send claw control signal to STM32 via UART5. Returns True on success."""
    try:
        with open(UART5_DEV, "wb") as f:
            f.write(b"\xFD\x88\x88\xFD")
            f.flush()
        print("[UART5] FD 88 88 FD", flush=True)
        return True
    except Exception as e:
        print(f"[UART5] Error: {e}", flush=True)
        return False


def uart_store(coord_y):
    """Send store command. Returns True on success."""
    if 11 <= coord_y <= 28:
        ok1 = send_uart4(0xFA, coord_y)
        time.sleep(0.5)
        ok2 = send_uart5_control()
        return ok1 and ok2
    print(f"[Store] Invalid coord: {coord_y}", flush=True)
    return False


def uart_retrieve(coord_y):
    """Send retrieve command. Returns True on success."""
    if 11 <= coord_y <= 28:
        ok1 = send_uart4(0xFB, coord_y)
        time.sleep(0.5)
        ok2 = send_uart5_control()
        return ok1 and ok2
    print(f"[Retrieve] Invalid coord: {coord_y}", flush=True)
    return False


def print_label(book_id, title):
    """直接调用 print_label.py 打印 50×50mm 标签."""
    try:
        result = subprocess.run(
            ["/userdata/python/bin/python3", "/userdata/book_app/print_label.py", str(book_id), title],
            timeout=10,
            check=False,
            capture_output=True,
            text=True,
        )
        if result.returncode != 0:
            print(f"[Print] FAIL ({result.returncode}): {result.stderr.strip()}", flush=True)
            return False
        print(f"[Print] {title} (id={book_id})", flush=True)
        return True
    except Exception as e:
        print(f"[Print] Error: {e}", flush=True)
        return False


def printer_calibrate():
    """Send EM20L calibration command via UART9."""
    try:
        with open("/dev/ttyS9", "wb") as f:
            f.write(b"\x1F\x63")
        print("[Printer] Calibration sent", flush=True)
        return True
    except Exception as e:
        print(f"[Printer] Calibrate error: {e}", flush=True)
        return False


# ==================== Task Queue ====================

import heapq

class TaskPriority:
    BORROW = 1       # highest — user waiting at shelf
    RETRIEVE = 2     # admin manual unload
    SHELVE = 3       # lowest — batch shelving

_task_queue_lock = threading.Lock()
_task_queue_cv = threading.Condition(_task_queue_lock)
_task_queue_heap = []  # (priority, seq, action, coord_y, event, result_box)
_task_queue_seq = 0
_task_queue_current = None
_task_queue_running = True


def _task_queue_worker():
    global _task_queue_current
    while _task_queue_running:
        with _task_queue_cv:
            while not _task_queue_heap and _task_queue_running:
                _task_queue_cv.wait(timeout=1)
            if not _task_queue_running:
                break
            _, _, action, coord_y, event, result_box = heapq.heappop(_task_queue_heap)
            _task_queue_current = (action, coord_y)

        try:
            if action in ("shelve",):
                ok = uart_store(coord_y)
            else:
                ok = uart_retrieve(coord_y)
            result_box["ok"] = ok
        except Exception as e:
            result_box["ok"] = False
            result_box["error"] = str(e)
        finally:
            with _task_queue_lock:
                _task_queue_current = None
            event.set()


def task_enqueue(priority: int, action: str, coord_y: int) -> dict:
    """Enqueue a mechanical claw task and block until completion. Returns result dict."""
    global _task_queue_seq
    event = threading.Event()
    result_box = {}
    with _task_queue_lock:
        _task_queue_seq += 1
        heapq.heappush(_task_queue_heap,
                       (priority, _task_queue_seq, action, coord_y, event, result_box))
        _task_queue_cv.notify()
    event.wait()
    return result_box.copy()


def task_queue_status() -> dict:
    with _task_queue_lock:
        tasks = []
        for p, s, a, c, e, rb in sorted(_task_queue_heap):
            tasks.append({"priority": p, "action": a, "coord_y": c})
        cur = None
        if _task_queue_current:
            cur = {"action": _task_queue_current[0], "coord_y": _task_queue_current[1]}
        return {"pending": len(tasks), "current": cur, "tasks": tasks}


# Start worker
_task_worker_thread = threading.Thread(target=_task_queue_worker, daemon=True)
_task_worker_thread.start()


# ==================== Flask App ====================
app = Flask(__name__)
CORS(app)

# Fix: Convert ints > JS safe integer (2^53) to strings in ALL JSON responses.
# Flask's jsonify internally uses json.dumps(cls=app.json_encoder). We override
# the encoder's iterencode to recursively convert large ints before serialization.
_JS_SAFE = 2**53

def _safe_convert(obj):
    """Recursively convert large ints to strings for JS compatibility."""
    if isinstance(obj, dict):
        return {k: _safe_convert(v) for k, v in obj.items()}
    if isinstance(obj, (list, tuple)):
        return [_safe_convert(v) for v in obj]
    if isinstance(obj, int) and (obj > _JS_SAFE or obj < -_JS_SAFE):
        return str(obj)
    return obj

class _SafeEncoder(json.JSONEncoder):
    def encode(self, o):
        return super().encode(_safe_convert(o))
    def iterencode(self, o, _one_shot=False):
        return super().iterencode(_safe_convert(o), _one_shot)

app.json_encoder = _SafeEncoder
# Capture original BEFORE patching
_orig_dumps = json.dumps

# Patch json.dumps globally (used by socketio, Flask internals)
json.dumps = lambda obj, **kw: _orig_dumps(_safe_convert(obj), **kw)

# Flask 2.2+ compat: also patch app.json provider if present
try:
    app.json.dumps = lambda obj, **kw: _orig_dumps(_safe_convert(obj), **kw)
except Exception:
    pass

socketio = SocketIO(app, cors_allowed_origins="*", async_mode="threading")

# Admin PIN — change via env or here
ADMIN_PIN = os.environ.get("ADMIN_PIN", "123456")

# ==================== Auth ====================

ADMIN_ENDPOINTS = {
    "/api/book/add",
    "/api/book-instance/scan",
    "/api/book-instance/retrieve",
    "/api/book-instance/re-shelve",
    "/api/storage-slot",
}

PUBLIC_ENDPOINTS = {
    "/api/health",
    "/api/book/device/status",
    "/api/users/login",
}

# Role constants aliased for convenience
ROLE_ADMIN = UserRole.ADMIN
ROLE_USER = UserRole.USER


def get_current_user():
    """Parse Bearer token and return user dict or None."""
    token = request.headers.get("Authorization", "").replace("Bearer ", "")
    if token and token.startswith("local_"):
        openid = token.replace("local_", "")
        return user_get_by_openid(openid)
    return None


def is_admin():
    u = get_current_user()
    return u is not None and u.get("role") == ROLE_ADMIN


def require_admin():
    """Check admin role; return error response if not admin."""
    u = get_current_user()
    if u is None:
        return jsonify({"code": 401, "message": "请先登录"}), 401
    if u.get("role") != ROLE_ADMIN:
        return jsonify({"code": 403, "message": "仅管理员可操作"}), 403
    return None  # OK

# ==================== WebSocket ====================

@socketio.on("connect")
def on_ws_connect():
    WS_CLIENTS.add(request.sid)
    emit("status", {"device": "esp32", "online": True})


@socketio.on("disconnect")
def on_ws_disconnect():
    WS_CLIENTS.discard(request.sid)


# ==================== Book API ====================

@app.route("/api/book/add", methods=["POST"])
def api_book_add():
    """创建书本 → 直接打印标签 → 创建 BookInstance（管理员）"""
    err = require_admin()
    if err:
        return err

    data = request.json or {}
    title = data.get("title", "").strip()
    category = data.get("category", "")

    if not title:
        return jsonify({"code": 400, "message": "书名为空"}), 400

    try:
        book = book_create(title=title, category=category)
        ok = print_label(book["id"], title)
        instance_create(book_id=book["id"], status=InstanceStatus.PENDING_STICKER)
        log_create(
            instance_id=0,
            action_type="PRINT",
            content=f"录入《{title}》 分类:{category}",
            status=1,
        )
        rsp = {"code": 200, "data": f"指令下发成功，书名：{title}"}
        if not ok:
            rsp["warning"] = "打印指令发送失败，请检查打印机"
        return jsonify(rsp)
    except Exception as e:
        return jsonify({"code": 500, "message": str(e)}), 500


# ==================== Device Status ====================

@app.route("/api/book/device/status", methods=["GET"])
def api_device_status():
    # Device IS the server — always online when responding
    return jsonify({"code": 200, "data": True})


# ==================== Storage Slot API ====================

@app.route("/api/storage-slot/list", methods=["POST", "GET"])
def api_slot_list():
    """返回全部 16 槽位状态"""
    try:
        slots = slot_list()
        return jsonify({"code": 200, "data": slots})
    except Exception as e:
        return jsonify({"code": 500, "message": str(e)}), 500


@app.route("/api/storage-slot", methods=["POST"])
def api_slot_create():
    err = require_admin()
    if err:
        return err
    data = request.json or {}
    try:
        db = __import__("db").get_db()
        ts = now_str()
        cur = db.execute(
            "INSERT INTO storage_slot (coord_x, coord_y, is_occupied, remark, create_time, update_time) "
            "VALUES (?, ?, ?, ?, ?, ?)",
            (data.get("coord_x", 0), data.get("coord_y", 0),
             data.get("is_occupied", 0), data.get("remark", ""), ts, ts),
        )
        db.commit()
        s = slot_get(cur.lastrowid)
        return jsonify({"code": 200, "data": s})
    except Exception as e:
        return jsonify({"code": 500, "message": str(e)}), 500


@app.route("/api/storage-slot/<int:slot_id>", methods=["GET"])
def api_slot_get(slot_id):
    s = slot_get(slot_id)
    if s is None:
        return jsonify({"code": 404, "message": "槽位不存在"}), 404
    return jsonify({"code": 200, "data": s})


@app.route("/api/storage-slot/<int:slot_id>", methods=["PUT"])
def api_slot_update(slot_id):
    data = request.json or {}
    try:
        s = slot_update(slot_id, **data)
        if s is None:
            return jsonify({"code": 404, "message": "槽位不存在"}), 404
        return jsonify({"code": 200, "data": s})
    except Exception as e:
        return jsonify({"code": 500, "message": str(e)}), 500


# ==================== Printer API ====================

@app.route("/api/printer/calibrate", methods=["POST"])
def api_printer_calibrate():
    """EM20L 打印机校准 — 发送校准指令到 UART9"""
    err = require_admin()
    if err:
        return err
    ok = printer_calibrate()
    if ok:
        return jsonify({"code": 200, "message": "校准指令已发送"})
    return jsonify({"code": 500, "message": "UART9 不可用"}), 500


@app.route("/api/printer/test", methods=["POST"])
def api_printer_test():
    """打印测试标签"""
    err = require_admin()
    if err:
        return err
    ts = __import__("datetime").datetime.now().strftime("%H:%M:%S")
    ok = print_label(0, f"测试标签 {ts}")
    if ok:
        return jsonify({"code": 200, "message": "测试标签已打印"})
    return jsonify({"code": 500, "message": "打印失败"}), 500


# ==================== Book Instance API ====================

@app.route("/api/book-instance", methods=["POST"])
def api_instance_create():
    data = request.json or {}
    try:
        inst = instance_create(
            book_id=data.get("book_id", 0),
            user_id=data.get("user_id", 0),
            slot_id=data.get("slot_id", 0),
            status=data.get("status", 0),
        )
        return jsonify({"code": 200, "data": inst})
    except Exception as e:
        return jsonify({"code": 500, "message": str(e)}), 500


@app.route("/api/book-instance/<int:instance_id>", methods=["GET"])
def api_instance_get(instance_id):
    print(f"[API] GET /api/book-instance/{instance_id} (type={type(instance_id).__name__})", flush=True)
    inst = instance_get(instance_id)
    if inst is None:
        print(f"[API] → 404 not found", flush=True)
        return jsonify({"code": 404, "message": "实例不存在"}), 404
    return jsonify({"code": 200, "data": inst})


@app.route("/api/book-instance/<int:instance_id>", methods=["DELETE"])
def api_instance_delete(instance_id):
    try:
        ok = instance_delete(instance_id)
        return jsonify({"code": 200, "data": ok})
    except Exception as e:
        return jsonify({"code": 500, "message": str(e)}), 500


@app.route("/api/book-instance/<int:instance_id>", methods=["PUT"])
def api_instance_update(instance_id):
    """修改实例状态等字段"""
    data = request.json or {}
    try:
        kwargs = {}
        if "status" in data: kwargs["status"] = int(data["status"])
        if "slot_id" in data: kwargs["slot_id"] = int(data["slot_id"])
        if "due_date" in data: kwargs["due_date"] = data["due_date"]
        result = instance_update(instance_id, **kwargs)
        if result is None:
            return jsonify({"code": 404, "message": "实例不存在"}), 404
        return jsonify({"code": 200, "data": result})
    except Exception as e:
        return jsonify({"code": 500, "message": str(e)}), 500


@app.route("/api/book-instance/page", methods=["POST"])
def api_instance_page():
    data = request.json or {}
    page = data.get("page", 1)
    size = data.get("size", 20)
    status = data.get("status", None)
    try:
        result = instance_page(page=page, size=size, status=status)
        return jsonify({"code": 200, "data": result})
    except Exception as e:
        return jsonify({"code": 500, "message": str(e)}), 500


@app.route("/api/book-instance/scan", methods=["POST"])
def api_instance_scan():
    """
    扫码入库 — 按分类分配槽位（管理员）
    请求: {bookId, status=3}
    返回: {instanceId, bookId, slotId, coordX, coordY, remark, status}
    """
    err = require_admin()
    if err:
        return err

    data = request.json or {}
    book_id = data.get("bookId", 0)
    status = data.get("status", InstanceStatus.ON_SHELF)

    if not book_id:
        return jsonify({"code": 400, "message": "bookId 不能为空"}), 400

    # Get book category for slot allocation
    book = book_get(book_id)
    category = book["category"] if book else ""

    try:
        result = scan_allocate(book_id=book_id, category=category, status=status)
        # UART via task queue — serialized mechanical claw control
        uart_result = task_enqueue(TaskPriority.SHELVE, "shelve", result["coordY"])
        log_create(
            instance_id=result["instanceId"],
            action_type="SCAN",
            content=f"入库《{book.get('title', '?')}》→ 槽位{result['slotId']} ({result['remark']})",
            status=1,
        )
        rsp = {"code": 200, "data": result}
        if not uart_result.get("ok"):
            rsp["warning"] = "UART指令发送失败，请检查机械爪"
        return jsonify(rsp)
    except RuntimeError as e:
        return jsonify({"code": 400, "message": str(e)}), 400
    except Exception as e:
        return jsonify({"code": 500, "message": str(e)}), 500


@app.route("/api/book-instance/retrieve", methods=["POST"])
def api_instance_retrieve():
    """管理员取书 → UART FB+坐标 → 机械爪推出"""
    err = require_admin()
    if err:
        return err

    data = request.json or {}
    instance_id = data.get("instanceId", 0)

    inst = instance_get(instance_id)
    if inst is None:
        return jsonify({"code": 404, "message": "实例不存在"}), 404
    if inst["status"] != InstanceStatus.ON_SHELF:
        return jsonify({"code": 400, "message": "该书不在架上"}), 400
    if inst["slot_id"] == 0:
        return jsonify({"code": 400, "message": "该书本未分配槽位"}), 400

    slot = slot_get(inst["slot_id"])
    if slot is None:
        return jsonify({"code": 404, "message": "槽位不存在"}), 404

    book = book_get(inst["book_id"])
    title = book["title"] if book else "?"

    # UART via task queue — serialized mechanical claw control
    uart_result = task_enqueue(TaskPriority.RETRIEVE, "retrieve", slot["coord_y"])

    log_create(
        instance_id=instance_id,
        action_type="RETRIEVE",
        content=f"取书《{title}》从 槽位{slot['slot_id']}",
        status=1,
    )

    rsp = {"code": 200, "data": {
        "instanceId": instance_id,
        "slotId": slot["slot_id"],
        "coordX": slot["coord_x"],
        "coordY": slot["coord_y"],
        "remark": slot["remark"],
    }}
    if not uart_result.get("ok"):
        rsp["warning"] = "UART指令发送失败，请检查机械爪"
    return jsonify(rsp)


@app.route("/api/book-instance/verify", methods=["POST"])
def api_instance_verify():
    """管理员回收确认 — 释放槽位"""
    err = require_admin()
    if err:
        return err

    data = request.json or {}
    instance_id = data.get("instanceId", 0)

    inst = instance_get(instance_id)
    if inst is None:
        return jsonify({"code": 404, "message": "实例不存在"}), 404

    slot_id = inst["slot_id"]
    if slot_id:
        slot_release(slot_id)

    instance_update(instance_id, slot_id=0, status=InstanceStatus.PENDING_STICKER)

    book = book_get(inst["book_id"])
    log_create(
        instance_id=instance_id,
        action_type="VERIFY",
        content=f"回收确认《{book.get('title', '?')}》释放槽位{slot_id}",
        status=1,
    )

    return jsonify({"code": 200, "data": "回收确认成功"})


# ==================== Book Search & Shelf ====================

@app.route("/api/book/search", methods=["GET"])
def api_book_search():
    """模糊搜索书名"""
    keyword = request.args.get("keyword", "").strip()
    if not keyword:
        return jsonify({"code": 400, "message": "keyword 不能为空"}), 400
    try:
        books = book_search(keyword)
        return jsonify({"code": 200, "data": books})
    except Exception as e:
        return jsonify({"code": 500, "message": str(e)}), 500


@app.route("/api/book-instance/shelf", methods=["GET"])
def api_shelf_browse():
    """当前在架书籍列表（含书名+坐标）"""
    try:
        items = instance_shelf_list()
        return jsonify({"code": 200, "data": items})
    except Exception as e:
        return jsonify({"code": 500, "message": str(e)}), 500


# ==================== Borrow / Return ====================

@app.route("/api/book-instance/borrow", methods=["POST"])
def api_borrow():
    """
    用户扫码借书：机械爪推出书本→用户拿走→状态→已借出→释放槽位
    请求: {instanceId, userId}
    """
    data = request.json or {}
    instance_id = data.get("instanceId", 0)
    user_id = data.get("userId", 0)

    if not instance_id:
        return jsonify({"code": 400, "message": "instanceId 不能为空"}), 400

    inst = instance_get(instance_id)
    if inst is None:
        return jsonify({"code": 404, "message": "实例不存在"}), 404
    if inst["status"] != InstanceStatus.ON_SHELF:
        return jsonify({"code": 400, "message": "该书不在架上，无法借阅"}), 400

    user = user_get(user_id) if user_id else None
    operator = user["nickname"] if user else ""
    book = book_get(inst["book_id"])
    title = book["title"] if book else "?"

    # Read coord before DB changes (slot gets released on borrow)
    coord_y = None
    if inst["slot_id"]:
        slot = slot_get(inst["slot_id"])
        if slot:
            coord_y = slot["coord_y"]

    # 1. DB first — mark borrowed and release slot
    try:
        result = borrow_instance(instance_id, user_id)
    except RuntimeError as e:
        return jsonify({"code": 400, "message": str(e)}), 400

    # 2. UART via task queue — push book out
    uart_ok = True
    if coord_y is not None:
        uart_result = task_enqueue(TaskPriority.BORROW, "borrow", coord_y)
        uart_ok = uart_result.get("ok", False)

    log_create(
        instance_id=instance_id,
        user_id=user_id,
        action_type="BORROW",
        content=f"借出《{title}》从 槽位{inst['slot_id']} · 借阅人: {operator or '用户'+str(user_id)}",
        operator_name=operator or f"用户{user_id}",
        status=1,
    )
    rsp = {"code": 200, "data": result}
    if not uart_ok:
        rsp["warning"] = "UART指令发送失败，请手动取书"
    return jsonify(rsp)


@app.route("/api/book-instance/return", methods=["POST"])
def api_return():
    """
    用户扫码还书：状态→待上架，重新分配槽位
    请求: {instanceId}
    """
    data = request.json or {}
    instance_id = data.get("instanceId", 0)

    if not instance_id:
        return jsonify({"code": 400, "message": "instanceId 不能为空"}), 400

    try:
        result = return_instance(instance_id)
        book = book_get(result["book_id"]) if result else None
        title = book["title"] if book else "?"
        log_create(
            instance_id=instance_id,
            action_type="RETURN",
            content=f"归还《{title}》→ 待上架",
            status=1,
        )
        return jsonify({"code": 200, "data": result})
    except RuntimeError as e:
        return jsonify({"code": 400, "message": str(e)}), 400


@app.route("/api/book-instance/re-shelve", methods=["POST"])
def api_re_shelve():
    """
    管理员重新上架：分配槽位→UART FA+坐标→机械爪推入
    请求: {instanceId}
    """
    err = require_admin()
    if err:
        return err

    data = request.json or {}
    instance_id = data.get("instanceId", 0)

    if not instance_id:
        return jsonify({"code": 400, "message": "instanceId 不能为空"}), 400

    inst = instance_get(instance_id)
    if inst is None:
        return jsonify({"code": 404, "message": "实例不存在"}), 404

    book = book_get(inst["book_id"])
    category = book["category"] if book else ""
    title = book["title"] if book else "?"

    try:
        result = re_shelve_instance(instance_id, category)
        uart_result = task_enqueue(TaskPriority.SHELVE, "shelve", result["coordY"])
        log_create(
            instance_id=instance_id,
            action_type="RE_SHELVE",
            content=f"重新上架《{title}》→ 槽位{result['slotId']} ({result['remark']})",
            status=1,
        )
        rsp = {"code": 200, "data": result}
        if not uart_result.get("ok"):
            rsp["warning"] = "UART指令发送失败，请检查机械爪"
        return jsonify(rsp)
    except RuntimeError as e:
        return jsonify({"code": 400, "message": str(e)}), 400


# ==================== Admin Shelve / Unshelve ====================

@app.route("/api/book-instance/admin-shelve", methods=["POST"])
def api_admin_shelve():
    """管理员上架单本书: 待贴标(0)/待上架(2) → 在架(3)
    UART 直控 (K230 #2 未上线时使用); 预留 K230 #2 路径。
    """
    err = require_admin()
    if err:
        return err

    data = request.json or {}
    instance_id = data.get("instanceId", 0)

    if not instance_id:
        return jsonify({"code": 400, "message": "instanceId 不能为空"}), 400

    inst = instance_get(instance_id)
    if inst is None:
        return jsonify({"code": 404, "message": "实例不存在"}), 404
    if inst["status"] != InstanceStatus.PENDING_SHELF:
        return jsonify({"code": 400, "message": f"状态={inst['status']}, 不需要上架"}), 400

    book = book_get(inst["book_id"])
    category = book.get("category", "") if book else ""
    title = book.get("title", "?") if book else "?"

    try:
        # 分配槽位 (如果还没分)
        if inst["status"] == InstanceStatus.PENDING_SHELF and inst.get("slot_id"):
            result = re_shelve_instance(instance_id, category)
        else:
            result = scan_allocate(book_id=inst.get("book_id", 0), category=category, status=InstanceStatus.ON_SHELF)

        # UART 直控推书
        uart_result = task_enqueue(TaskPriority.SHELVE, "shelve", result["coordY"])

        log_create(
            instance_id=instance_id,
            action_type="ADMIN_SHELVE",
            content=f"管理员上架《{title}》→ 槽位{result['slotId']} ({result['remark']})",
            status=1,
        )
        rsp = {"code": 200, "data": result}
        if not uart_result.get("ok"):
            rsp["warning"] = "UART指令发送失败"
        return jsonify(rsp)
    except RuntimeError as e:
        return jsonify({"code": 400, "message": str(e)}), 400
    except Exception as e:
        return jsonify({"code": 500, "message": str(e)}), 500


@app.route("/api/book-instance/admin-unshelve", methods=["POST"])
def api_admin_unshelve():
    """管理员下架单本书: 在架(3) → 待贴标(0), 释放槽位
    UART 直控 (K230 #2 未上线时使用); 预留 K230 #2 FETCH 路径。
    """
    err = require_admin()
    if err:
        return err

    data = request.json or {}
    instance_id = data.get("instanceId", 0)

    if not instance_id:
        return jsonify({"code": 400, "message": "instanceId 不能为空"}), 400

    inst = instance_get(instance_id)
    if inst is None:
        return jsonify({"code": 404, "message": "实例不存在"}), 404
    if inst["status"] != InstanceStatus.ON_SHELF:
        return jsonify({"code": 400, "message": "该书不在架上"}), 400

    slot = slot_get(inst["slot_id"]) if inst.get("slot_id") else None
    coord_y = slot["coord_y"] if slot else None

    if not coord_y:
        return jsonify({"code": 400, "message": "该书本未分配槽位"}), 400

    book = book_get(inst["book_id"])
    title = book.get("title", "?") if book else "?"

    # UART 直控取书
    uart_result = task_enqueue(TaskPriority.RETRIEVE, "retrieve", coord_y)

    # 释放槽位 + 状态→待贴标
    if inst.get("slot_id"):
        slot_release(inst["slot_id"])
    instance_update(instance_id, slot_id=0, status=InstanceStatus.PENDING_STICKER)

    log_create(
        instance_id=instance_id,
        action_type="ADMIN_UNSHELVE",
        content=f"管理员下架《{title}》从 槽位{inst.get('slot_id', '?')}",
        status=1,
    )
    rsp = {"code": 200, "data": {
        "instanceId": instance_id,
        "coordY": coord_y,
        "status": InstanceStatus.PENDING_STICKER,
    }}
    if not uart_result.get("ok"):
        rsp["warning"] = "UART指令发送失败，请手动取书"
    return jsonify(rsp)


# ==================== Admin Batch Shelve Flow ====================

@app.route("/api/admin/start-shelve", methods=["POST"])
def api_admin_start_shelve():
    """管理员批量上架: 触发整书区推书→扫码→循环上架。
    K230 #2 未上线时返回提示; 上线后调用 k230_bridge 的 ReturnFlowManager.start_admin_shelve()。
    """
    err = require_admin()
    if err:
        return err

    try:
        from k230_bridge_v2 import get_bridge
        bridge = get_bridge()
        if bridge.return_flow.is_active:
            return jsonify({"code": 400, "message": f"上架流程已在运行 ({bridge.return_flow.state.value})"}), 400

        ok = bridge.return_flow.start_admin_shelve(admin_user_id=0)
        if ok:
            return jsonify({"code": 200, "data": {"state": "started", "message": "上架流程已启动"}})
        else:
            return jsonify({"code": 500, "message": "启动失败"}), 500
    except ImportError:
        return jsonify({"code": 503, "message": "K230 桥接服务未运行，请使用单本上架 (admin-shelve)"}), 503


@app.route("/api/admin/shelve-status", methods=["GET"])
def api_admin_shelve_status():
    """查询批量上架流程状态"""
    try:
        from k230_bridge_v2 import get_bridge
        bridge = get_bridge()
        status = bridge.return_flow.get_admin_shelve_status()
        return jsonify({"code": 200, "data": status})
    except ImportError:
        return jsonify({"code": 200, "data": {"state": "idle", "message": "K230 桥接离线"}})
def api_my_borrowed():
    """我的借阅列表"""
    user_id = request.args.get("userId", 0, type=int)
    if not user_id:
        return jsonify({"code": 400, "message": "userId 不能为空"}), 400
    try:
        items = instance_borrowed_list(user_id=user_id)
        return jsonify({"code": 200, "data": items})
    except Exception as e:
        return jsonify({"code": 500, "message": str(e)}), 500


# ==================== Slot Lookup ====================

@app.route("/api/book-instance/by-slot/<int:slot_id>", methods=["GET"])
def api_instance_by_slot(slot_id):
    """查询某槽位上的书籍"""
    inst = instance_get_by_slot(slot_id)
    if inst is None:
        return jsonify({"code": 200, "data": None})
    book = book_get(inst["book_id"])
    return jsonify({"code": 200, "data": {
        **inst,
        "title": book["title"] if book else "",
    }})


# ==================== User API ====================

@app.route("/api/users/login", methods=["POST"])
def api_user_login():
    """微信登录 — 简化版，返回用户信息"""
    data = request.json or {}
    openid = data.get("code", "")  # 实际应通过微信 code2session 换取 openid
    if not openid:
        return jsonify({"code": 400, "message": "code 不能为空"}), 400

    user = user_get_by_openid(openid)
    if user is None:
        user = user_create(openid=openid, nickname=data.get("nickname", ""),
                           avatar_url=data.get("avatarUrl", ""), role=0)
    return jsonify({"code": 200, "data": {"token": f"local_{openid}", "user": user}})


@app.route("/api/users/me", methods=["GET"])
def api_user_me():
    token = request.headers.get("Authorization", "").replace("Bearer ", "")
    if not token or not token.startswith("local_"):
        return jsonify({"code": 401, "message": "未登录"}), 401
    openid = token.replace("local_", "")
    user = user_get_by_openid(openid)
    if user is None:
        return jsonify({"code": 404, "message": "用户不存在"}), 404
    return jsonify({"code": 200, "data": user})


@app.route("/api/users/page", methods=["POST"])
def api_user_page():
    data = request.json or {}
    page = data.get("page", 1)
    size = data.get("size", 20)
    try:
        result = user_list(page=page, size=size)
        return jsonify({"code": 200, "data": result})
    except Exception as e:
        return jsonify({"code": 500, "message": str(e)}), 500


@app.route("/api/users/promote", methods=["POST"])
def api_user_promote():
    """提升为管理员：验证 PIN 码 → 设置 role=1"""
    data = request.json or {}
    user_id = data.get("userId", 0)
    pin = data.get("pin", "")

    if not user_id:
        return jsonify({"code": 400, "message": "userId 不能为空"}), 400
    if pin != ADMIN_PIN:
        return jsonify({"code": 403, "message": "PIN 码错误"}), 403

    user = user_get(user_id)
    if user is None:
        return jsonify({"code": 404, "message": "用户不存在"}), 404

    db = __import__("db").get_db()
    ts = now_str()
    db.execute("UPDATE user SET role=1, update_time=? WHERE user_id=?", (ts, user_id))
    db.commit()

    log_create(
        user_id=user_id,
        action_type="PROMOTE",
        content=f"提升用户 {user.get('nickname', user_id)} 为管理员",
        status=1,
    )
    return jsonify({"code": 200, "data": user_get(user_id)})


@app.route("/api/users/demote", methods=["POST"])
def api_user_demote():
    """降级为普通用户：清除声纹数据 → 设置 role=0"""
    data = request.json or {}
    user_id = data.get("userId", 0)
    pin = data.get("pin", "")

    if not user_id:
        return jsonify({"code": 400, "message": "userId 不能为空"}), 400
    if pin != ADMIN_PIN:
        return jsonify({"code": 403, "message": "PIN 码错误"}), 403

    user = user_get(user_id)
    if user is None:
        return jsonify({"code": 404, "message": "用户不存在"}), 404
    if user.get("role") != 1:
        return jsonify({"code": 400, "message": "该用户不是管理员"}), 400

    # Delete voiceprint files
    name = (user.get("nickname") or "").strip()
    voice_dir = "/userdata/audio/voiceprints"
    if name and os.path.isdir(voice_dir):
        vp_file = os.path.join(voice_dir, f"{name}.npy")
        if os.path.exists(vp_file):
            os.remove(vp_file)
        # Also check voice_web_app.py voiceprint dir
        vp2_dir = "/userdata/voice_app/voiceprints"
        if os.path.isdir(vp2_dir):
            vp2_file = os.path.join(vp2_dir, f"{name}.npy")
            if os.path.exists(vp2_file):
                os.remove(vp2_file)

    db = __import__("db").get_db()
    ts = now_str()
    db.execute("UPDATE user SET role=0, update_time=? WHERE user_id=?", (ts, user_id))
    db.commit()

    log_create(
        user_id=user_id,
        action_type="DEMOTE",
        content=f"降级用户 {name or user_id} 为普通用户",
        status=1,
    )
    return jsonify({"code": 200, "data": user_get(user_id)})


@app.route("/api/users/freeze", methods=["POST"])
def api_user_freeze():
    """冻结/解冻用户"""
    data = request.json or {}
    user_id = data.get("userId", 0)
    frozen = data.get("frozen", 0)  # 1=冻结, 0=解冻
    pin = data.get("pin", "")

    if not user_id:
        return jsonify({"code": 400, "message": "userId 不能为空"}), 400
    if pin != ADMIN_PIN:
        return jsonify({"code": 403, "message": "PIN 码错误"}), 403

    user = user_get(user_id)
    if user is None:
        return jsonify({"code": 404, "message": "用户不存在"}), 404

    db = __import__("db").get_db()
    ts = now_str()
    db.execute("UPDATE user SET is_frozen=?, update_time=? WHERE user_id=?", (frozen, ts, user_id))
    db.commit()

    return jsonify({"code": 200, "data": user_get(user_id)})


@app.route("/api/users/pay-fine", methods=["POST"])
def api_user_pay_fine():
    """Clear user fine balance. Called by QR scanner after payment QR scanned."""
    data = request.json or {}
    user_id = data.get("userId", 0)
    if not user_id:
        return jsonify({"code": 400, "message": "userId 不能为空"}), 400

    u = user_get(user_id)
    if not u:
        return jsonify({"code": 404, "message": "用户不存在"}), 404

    fine = u.get("fine_balance", 0)
    if fine <= 0:
        return jsonify({"code": 200, "data": {"message": "无欠费", "paid": 0}})

    db = __import__("db").get_db()
    ts = now_str()
    db.execute("UPDATE user SET fine_balance = 0, update_time = ? WHERE user_id = ?", (ts, user_id))
    db.commit()

    log_create(user_id=user_id, action_type="PAY_FINE",
               content=f"缴纳罚款 ¥{fine:.1f}，已结清",
               operator_name=u.get("nickname") or f"用户{user_id}", status=1)
    return jsonify({"code": 200, "data": {"message": f"已缴纳 ¥{fine:.1f}，欠费已结清", "paid": round(fine, 2)}})


@app.route("/api/users/<int:user_id>", methods=["PUT"])
def api_user_update(user_id):
    """Update user fields: nickname, avatar_url, role."""
    data = request.json or {}
    allowed = {k: v for k, v in data.items() if k in ("nickname", "avatar_url", "role")}
    if not allowed:
        return jsonify({"code": 400, "message": "无可更新的字段"}), 400
    u = user_update(user_id, **allowed)
    if u:
        return jsonify({"code": 200, "data": u})
    return jsonify({"code": 404, "message": "用户不存在"}), 404


# ==================== Operation Log API ====================

@app.route("/api/operation-log/page", methods=["POST"])
def api_log_page():
    data = request.json or {}
    page = data.get("page", 1)
    size = data.get("size", 20)
    try:
        result = log_page(page=page, size=size)
        return jsonify({"code": 200, "data": result})
    except Exception as e:
        return jsonify({"code": 500, "message": str(e)}), 500


@app.route("/api/operation-log/by-instance/<int:instance_id>", methods=["GET"])
def api_log_by_instance(instance_id):
    """某本书的全部操作记录"""
    try:
        logs = log_by_instance(instance_id)
        return jsonify({"code": 200, "data": logs})
    except Exception as e:
        return jsonify({"code": 500, "message": str(e)}), 500


# ==================== Voice AI Proxy ====================

import requests as http_requests
import urllib3
urllib3.disable_warnings(urllib3.exceptions.InsecureRequestWarning)

VOICE_API = "https://127.0.0.1:8890"


@app.route("/api/voice/<path:subpath>", methods=["GET", "POST"])
def voice_proxy(subpath):
    """Proxy AI voice API calls to voice_web_app.py :8890"""
    url = f"{VOICE_API}/api/{subpath}"
    try:
        if request.method == "GET":
            resp = http_requests.get(url, params=request.args, verify=False, timeout=15)
        else:
            resp = http_requests.post(url, json=request.get_json(silent=True) or {}, verify=False, timeout=90)
        return jsonify(resp.json()), resp.status_code
    except http_requests.ConnectionError:
        return jsonify({"ok": False, "error": "语音服务离线"}), 502
    except Exception as e:
        return jsonify({"ok": False, "error": str(e)}), 502


# ==================== Screen Dashboard ====================

@app.route("/screen", methods=["GET"])
def screen_dashboard():
    """管理员屏幕界面——浏览器打开全屏使用"""
    screen_path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "screen.html")
    if os.path.exists(screen_path):
        with open(screen_path, "r", encoding="utf-8") as f:
            return f.read(), 200, {"Content-Type": "text/html; charset=utf-8"}
    return "<h1>screen.html not found</h1>", 404


@app.route("/static/<path:filename>")
def static_files(filename):
    """Serve static files (JS, CSS) for screen.html."""
    static_dir = os.path.join(os.path.dirname(os.path.abspath(__file__)), "static")
    return send_from_directory(static_dir, filename)


# ==================== K230 Bridge Status ====================

@app.route("/api/admin/exec", methods=["POST"])
def api_admin_exec():
    """管理员远程执行系统命令 (紧急维护用, 需 Bearer Token 鉴权)"""
    err = require_admin()
    if err:
        return err
    data = request.json or {}
    cmd = (data.get("cmd") or "").strip()
    if not cmd:
        return jsonify({"code": 400, "message": "cmd required"}), 400
    # 白名单: 仅允许重启特定服务
    allowed = ["/etc/init.d/S50dropbear restart", "/etc/init.d/S99book restart",
               "/etc/init.d/S99k230 restart", "/etc/init.d/S99a-voice restart",
               "/etc/init.d/S50dropbear start", "netstat -tln"]
    if cmd not in allowed:
        return jsonify({"code": 403, "message": f"命令不允许，仅限白名单: {allowed}"}), 403
    try:
        result = subprocess.run(cmd, shell=True, capture_output=True, timeout=15)
        return jsonify({"code": 200, "data": {
            "cmd": cmd,
            "stdout": result.stdout.decode("utf-8", errors="replace")[:2000],
            "stderr": result.stderr.decode("utf-8", errors="replace")[:2000],
            "rc": result.returncode,
        }})
    except Exception as e:
        return jsonify({"code": 500, "message": str(e)}), 500


@app.route("/api/k230/status", methods=["GET"])
def api_k230_status():
    """Return K230 bridge status (reads status file from bridge v2 process)."""
    try:
        import json
        with open("/tmp/k230_bridge_status.json", "r") as f:
            return jsonify({"code": 200, "data": json.load(f)})
    except Exception:
        try:
            from k230_bridge import get_status
            return jsonify({"code": 200, "data": get_status()})
        except ImportError:
            return jsonify({"code": 500, "message": "k230_bridge module not loaded"})


def _admin_cmd_to_bridge(cmd: str) -> tuple:
    """Send ADMIN command to bridge v2 via TCP (won't hijack K230 #1 connection)."""
    import socket
    sock = None
    try:
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.settimeout(3)
        sock.connect(("127.0.0.1", 9998))
        sock.sendall((cmd + "\n").encode("ascii"))
        data = b""
        while b"\n" not in data:
            chunk = sock.recv(128)
            if not chunk:
                break
            data += chunk
        resp = data.decode("ascii", errors="replace").strip()
        return True, resp
    except Exception as e:
        return False, str(e)
    finally:
        if sock:
            try: sock.close()
            except: pass


@app.route("/api/k230/wake", methods=["POST"])
def api_k230_wake():
    """Wake K230 #1 to start scanning. Sends ADMIN:WAKE to bridge via TCP."""
    ok, resp = _admin_cmd_to_bridge("ADMIN:WAKE")
    if ok and resp == "ACK":
        return jsonify({"code": 200, "message": "WAKE sent to K230 #1"})
    elif ok:
        return jsonify({"code": 503, "message": f"Bridge replied: {resp}"}), 503
    else:
        return jsonify({"code": 503, "message": f"Bridge unreachable: {resp}"}), 503


@app.route("/api/k230/sleep", methods=["POST"])
def api_k230_sleep():
    """Sleep K230 #1 to stop scanning. Sends ADMIN:SLEEP to bridge via TCP."""
    ok, resp = _admin_cmd_to_bridge("ADMIN:SLEEP")
    if ok and resp == "ACK":
        return jsonify({"code": 200, "message": "SLEEP sent to K230 #1"})
    elif ok:
        return jsonify({"code": 503, "message": f"Bridge replied: {resp}"}), 503
    else:
        return jsonify({"code": 503, "message": f"Bridge unreachable: {resp}"}), 503


@app.route("/api/k230/lookup/<int:book_id>", methods=["GET"])
def api_k230_lookup(book_id):
    """Test K230 book lookup without triggering navigation."""
    try:
        from k230_bridge import lookup_book
        return jsonify({"code": 200, "data": lookup_book(book_id)})
    except ImportError:
        return jsonify({"code": 500, "message": "k230_bridge module not loaded"})


# ==================== Task API (QR-based borrow/return) ====================

@app.route("/api/task/create", methods=["POST"])
def api_task_create():
    """Create a borrow/return task. Returns task_id for QR generation.
    请求: {action: "borrow"/"return", userId: int, payload: {instanceId} 或 {instanceIds: [...]}}
    响应: {code:200, data: {taskId, action, status, qrContent, expiresAt}}
    """
    data = request.json or {}
    action = data.get("action", "").strip().lower()
    user_id = data.get("userId", 0)
    payload = data.get("payload", {})

    if not action:
        return jsonify({"code": 400, "message": "action 不能为空"}), 400
    if action not in ("borrow", "return"):
        return jsonify({"code": 400, "message": f"不支持的操作: {action}"}), 400
    if not user_id:
        return jsonify({"code": 400, "message": "userId 不能为空"}), 400

    try:
        task = task_module.task_create(action, user_id, payload)
        return jsonify({"code": 200, "data": {
            "taskId": task["task_id"],
            "action": task["action"],
            "status": task["status"],
            "qrContent": task["qr_content"],
            "expiresAt": task["expires_at"],
        }})
    except ValueError as e:
        return jsonify({"code": 400, "message": str(e)}), 400
    except Exception as e:
        return jsonify({"code": 500, "message": str(e)}), 500


@app.route("/api/task/<task_id>/status", methods=["GET"])
def api_task_status(task_id):
    """Poll task status. Used by mini program after showing QR code.
    响应: {code:200, data: {taskId, status, action, result}}
    """
    task = task_module.task_get(task_id)
    if task is None:
        return jsonify({"code": 404, "message": "任务不存在"}), 404
    return jsonify({"code": 200, "data": {
        "taskId": task["task_id"],
        "action": task["action"],
        "status": task["status"],
        "qrContent": task.get("qr_content", ""),
        "result": task.get("result", {}),
        "createdAt": task["created_at"],
        "expiresAt": task["expires_at"],
    }})


@app.route("/api/task/list", methods=["GET"])
def api_task_list():
    """List recent tasks (for admin monitoring)."""
    status = request.args.get("status", None)
    tasks = task_module.task_list(status=status)
    return jsonify({"code": 200, "data": tasks})


# ==================== Camera QR Scan (internal endpoint) ====================

@app.route("/api/qr/scan", methods=["POST"])
def api_qr_scan():
    """Called by qr_scanner.py when a QR code is detected by OV5695.
    Request: {qrContent: "{\"t\":\"task_id\",\"a\":\"borrow\",\"ts\":123}"}
    Returns action to execute; scanner handles mechanical execution separately.
    """
    data = request.json or {}
    qr_content = data.get("qrContent", "").strip()

    if not qr_content:
        return jsonify({"code": 400, "message": "qrContent 不能为空"}), 400

    # Parse QR content
    try:
        qr_data = json.loads(qr_content)
        task_id = qr_data.get("t", "")
    except json.JSONDecodeError:
        return jsonify({"code": 400, "message": "无效的二维码格式"}), 400

    if not task_id:
        return jsonify({"code": 400, "message": "二维码缺少 task_id"}), 400

    # Claim task (atomically sets pending→processing)
    task = task_module.task_claim(task_id)
    if task is None:
        # Check current status
        existing = task_module.task_get(task_id)
        if existing is None:
            return jsonify({"code": 404, "message": "任务不存在"}), 404
        if existing["status"] == "expired":
            return jsonify({"code": 410, "message": "任务已过期"}), 410
        return jsonify({"code": 409, "message": f"任务状态: {existing['status']}"}), 409

    # Execute based on action
    try:
        user_id = task["user_id"]
        payload = task["payload"]

        if task["action"] == "borrow":
            instance_id = int(payload.get("instanceId", 0))
            inst = instance_get(instance_id)
            book = book_get(inst["book_id"]) if inst else None
            title = book["title"] if book else "?"

            # Get coord for UART
            coord_y = None
            if inst and inst.get("slot_id"):
                slot = slot_get(inst["slot_id"])
                if slot:
                    coord_y = slot["coord_y"]

            # DB first
            result = borrow_instance(instance_id, user_id)

            # UART via task queue
            if coord_y is not None:
                uart_result = task_enqueue(TaskPriority.BORROW, "borrow", coord_y)

            user_name = user_get(user_id)
            operator = user_name["nickname"] if user_name else ""
            log_create(
                instance_id=instance_id,
                user_id=user_id,
                action_type="QR_BORROW",
                content=f"扫码借书《{title}》 · 借阅人: {operator or '用户'+str(user_id)}",
                operator_name=operator or f"用户{user_id}",
                status=1,
            )
            task_module.task_complete(task_id, {
                "ok": True,
                "message": f"《{title}》借书成功，请在取书区取走书本",
                "title": title,
                "dueDate": result.get("due_date", ""),
            })

        elif task["action"] == "return":
            # Return mode: K230 #1 scans books in sorting area → sends book_id via TCP:9998
            # For now, process all borrowed books and accumulate fines
            from db import user_add_fine
            borrowed = instance_borrowed_list(user_id=user_id)
            instance_ids = [b["instance_id"] for b in borrowed]

            results = []
            total_fine = 0.0
            for iid in instance_ids:
                try:
                    result = return_instance(iid)
                    inst = instance_get(iid)
                    book = book_get(inst["book_id"]) if inst else None
                    title = book["title"] if book else "?"
                    fine = result.get("overdue_fine", 0)
                    if fine > 0:
                        user_add_fine(user_id, fine)
                        total_fine += fine
                    results.append({
                        "instanceId": iid, "ok": True, "title": title,
                        "overdueDays": result.get("overdue_days", 0),
                        "overdueFine": fine,
                    })
                    u_info = user_get(user_id)
                    op_name = u_info["nickname"] if u_info and u_info.get("nickname") else f"用户{user_id}"
                    log_create(
                        instance_id=iid, user_id=user_id,
                        action_type="QR_RETURN",
                        content=f"扫码还书《{title}》· 归还人: {op_name}" + (f" · 逾期罚款¥{fine:.1f}" if fine > 0 else ""),
                        operator_name=op_name, status=1,
                    )
                except Exception as e:
                    results.append({"instanceId": iid, "ok": False, "error": str(e)})

            done = sum(1 for r in results if r["ok"])
            msg = f"还书完成: {done}本"
            if total_fine > 0:
                msg += f" · 逾期罚款: ¥{total_fine:.2f}，已计入账户"

            task_module.task_complete(task_id, {
                "ok": True, "message": msg, "books": results,
                "doneCount": done, "totalCount": len(results),
                "totalFine": round(total_fine, 2),
            })

        return jsonify({"code": 200, "data": {
            "taskId": task_id,
            "status": "done",
            "result": task_module.task_get(task_id).get("result", {}),
        }})

    except RuntimeError as e:
        task_module.task_fail(task_id, str(e))
        return jsonify({"code": 400, "message": str(e)}), 400
    except Exception as e:
        task_module.task_fail(task_id, str(e))
        return jsonify({"code": 500, "message": str(e)}), 500


# ==================== IME (Pinyin → Hanzi lookup) ====================

_PINYIN_REVERSE = {}   # pinyin → [chars sorted by frequency]
_CHAR_FREQ_RANK = {}   # char → frequency rank (0=most common)
_CHAR_BIGRAMS = {}     # char → {next_char: weight} for word association

def _build_pinyin_reverse():
    """Build reverse pinyin→characters map at startup."""
    global _PINYIN_REVERSE, _CHAR_FREQ_RANK, _CHAR_BIGRAMS
    try:
        from pypinyin.pinyin_dict import pinyin_dict
        import unicodedata

        # Load frequency ranking if available
        rank_file = os.path.join(os.path.dirname(os.path.abspath(__file__)), "char_rank.json")
        try:
            with open(rank_file, "r", encoding="utf-8") as f:
                _CHAR_FREQ_RANK = json.load(f)
            print(f"[IME] Loaded {len(_CHAR_FREQ_RANK)} char frequency ranks", flush=True)
        except Exception:
            print("[IME] No frequency rank file, falling back to code-point order", flush=True)

        # Build character frequency from phrases_dict (simplified Chinese oriented)
        char_freq_from_phrases = {}
        try:
            from pypinyin.phrases_dict import phrases_dict as _pd
            for phrase in _pd:
                for ch in phrase:
                    if '一' <= ch <= '鿿':
                        char_freq_from_phrases[ch] = char_freq_from_phrases.get(ch, 0) + 1
            print(f"[IME] Phrase-based char freq: {len(char_freq_from_phrases)} chars", flush=True)
        except ImportError:
            pass

        # Build bigrams from phrases_dict (2-char sequences)
        try:
            for phrase in _pd:
                for i in range(len(phrase) - 1):
                    a, b = phrase[i], phrase[i + 1]
                    if not ('一' <= a <= '鿿' and '一' <= b <= '鿿'):
                        continue
                    if a not in _CHAR_BIGRAMS:
                        _CHAR_BIGRAMS[a] = {}
                    _CHAR_BIGRAMS[a][b] = _CHAR_BIGRAMS[a].get(b, 0) + 1
            print(f"[IME] Bigrams: {len(_CHAR_BIGRAMS)} chars with associations", flush=True)
        except Exception:
            pass

        tmp = {}
        for code, py_str in pinyin_dict.items():
            char = chr(code)
            for reading in py_str.split(","):
                reading = reading.strip()
                reading = reading.replace("ü", "v").replace("ǖ", "v").replace("ǘ", "v") \
                                 .replace("ǚ", "v").replace("ǜ", "v")
                nfkd = unicodedata.normalize("NFKD", reading)
                base = "".join(c for c in nfkd if not unicodedata.combining(c))
                if not base:
                    continue
                if base not in tmp:
                    tmp[base] = set()
                tmp[base].add(char)

        def _char_rank(c):
            o = ord(c)
            # Priority: phrase frequency > Taiwan rank > code-point position
            pf = char_freq_from_phrases.get(c, 0)
            freq = _CHAR_FREQ_RANK.get(c, 99999)
            if 0x4E00 <= o <= 0x9FFF:
                block = 0
            elif 0x3400 <= o <= 0x4DBF:
                block = 1
            elif o >= 0x20000:
                block = 3
            else:
                block = 2
            # Higher phrase freq = better, lower Taiwan rank = better
            return (block, -pf, freq, o)

        for base in tmp:
            _PINYIN_REVERSE[base] = "".join(sorted(tmp[base], key=_char_rank)[:30])
        print(f"[IME] Pinyin reverse map: {len(_PINYIN_REVERSE)} syllables", flush=True)
    except ImportError:
        print("[IME] pypinyin not installed, IME disabled", flush=True)


@app.route("/api/ime/pinyin", methods=["GET"])
def api_ime_pinyin():
    """Convert pinyin syllable to candidate Chinese characters.
    GET /api/ime/pinyin?q=zhong&prev=三 → {candidates: ...} with word association boost
    """
    q = request.args.get("q", "").strip().lower()
    prev = request.args.get("prev", "").strip()
    if not q:
        return jsonify({"code": 200, "candidates": ""})
    if not _PINYIN_REVERSE:
        return jsonify({"code": 200, "candidates": ""})

    chars = _PINYIN_REVERSE.get(q, "")
    if not chars:
        return jsonify({"code": 200, "candidates": ""})

    # If previous character is provided, boost associated next characters
    if prev and len(prev) == 1 and prev in _CHAR_BIGRAMS:
        bigrams = _CHAR_BIGRAMS[prev]
        # Get top associated chars sorted by weight
        top_assoc = sorted(bigrams.keys(), key=lambda c: -bigrams[c])[:10]
        # Reorder: associated chars first, then the rest
        assoc_set = set(top_assoc)
        boosted = [c for c in top_assoc if c in chars]
        rest = [c for c in chars if c not in assoc_set]
        chars = "".join(boosted + rest)

    return jsonify({"code": 200, "candidates": chars})


# ==================== Health ====================

@app.route("/api/health", methods=["GET"])
def api_health():
    return jsonify({"code": 200, "data": "ok", "mode": "local"})


@app.route("/api/task-queue/status", methods=["GET"])
def api_task_queue_status():
    """Return mechanical claw task queue status."""
    return jsonify({"code": 200, "data": task_queue_status()})


# ==================== Main ====================

if __name__ == "__main__":
    import argparse
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", type=int, default=9777)
    parser.add_argument("--db", default="/userdata/book_app/book.db")
    parser.add_argument("--ssl-cert", default=None)
    parser.add_argument("--ssl-key", default=None)
    args = parser.parse_args()

    # Override DB path before init
    import db
    db.DB_PATH = args.db
    os.makedirs(os.path.dirname(args.db), exist_ok=True)
    init_db()
    print(f"[DB] SQLite ready at {args.db}", flush=True)

    # Build IME reverse pinyin map
    _build_pinyin_reverse()

    ssl_context = None
    if args.ssl_cert and args.ssl_key:
        ssl_context = (args.ssl_cert, args.ssl_key)
        print(f"[SSL] HTTPS enabled", flush=True)

    # Start task cleanup background thread
    task_module.start_cleanup()

    print(f"[App] Starting on port {args.port}", flush=True)
    try:
        socketio.run(app, host="0.0.0.0", port=args.port,
                     ssl_context=ssl_context, allow_unsafe_werkzeug=True)
    finally:
        task_module.stop_cleanup()
