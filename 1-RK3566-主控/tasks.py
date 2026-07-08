"""
Task system for mini program QR-based borrow/return.
Mini program creates a task → gets task_id → generates QR → user shows QR to camera
→ camera scans QR → RK3566 executes task → mini program polls for completion.

Tasks expire after 5 minutes (configurable). One-time use only.
"""
import json
import os
import time
import uuid
import threading
import logging

logger = logging.getLogger("tasks")

# ==================== Config ====================
TASK_EXPIRE_SECONDS = 300  # 5 minutes
TASK_CLEANUP_INTERVAL = 60  # every 60 seconds

# ==================== In-memory store with SQLite persistence ====================

_task_lock = threading.Lock()
_tasks = {}  # task_id → task_dict (in-memory cache)

# Try to import db for persistence
try:
    import db as _db_module
    _DB_AVAILABLE = True
except ImportError:
    _DB_AVAILABLE = False


def _ensure_task_table():
    """Create task table in SQLite if it doesn't exist."""
    if not _DB_AVAILABLE:
        return
    try:
        conn = _db_module.get_db()
        conn.execute("""
            CREATE TABLE IF NOT EXISTS task (
                task_id   TEXT PRIMARY KEY,
                action    TEXT NOT NULL,
                user_id   INTEGER NOT NULL,
                payload   TEXT DEFAULT '{}',
                status    TEXT DEFAULT 'pending',
                result    TEXT DEFAULT '{}',
                created_at TEXT NOT NULL,
                expires_at TEXT NOT NULL,
                completed_at TEXT DEFAULT ''
            )
        """)
        conn.commit()
    except Exception as e:
        logger.warning(f"Task table init failed (non-fatal): {e}")


def _persist_task(task: dict):
    """Save task to SQLite."""
    if not _DB_AVAILABLE:
        return
    try:
        conn = _db_module.get_db()
        conn.execute(
            "INSERT OR REPLACE INTO task (task_id, action, user_id, payload, "
            "status, result, created_at, expires_at, completed_at) "
            "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)",
            (task["task_id"], task["action"], task["user_id"],
             json.dumps(task["payload"], ensure_ascii=False),
             task["status"], json.dumps(task["result"], ensure_ascii=False),
             task["created_at"], task["expires_at"], task.get("completed_at", "")),
        )
        conn.commit()
    except Exception as e:
        logger.warning(f"Task persist failed (non-fatal): {e}")


# ==================== Public API ====================

def task_create(action: str, user_id: int, payload: dict) -> dict:
    """Create a new task. Validates payload based on action type.

    Args:
        action: "borrow" or "return"
        user_id: the user initiating the task
        payload: {"instanceId": int} for borrow, {"instanceIds": [int,...]} for return

    Returns:
        task dict with task_id, action, status, qr_content, expires_at

    Raises:
        ValueError on invalid request
    """
    action = action.strip().lower()
    if action not in ("borrow", "return"):
        raise ValueError(f"Unknown action: {action}")

    # Validate payload
    if action == "borrow":
        instance_id = payload.get("instanceId", 0)
        if not instance_id:
            raise ValueError("借书需要指定 instanceId")
        from db import instance_get, user_is_frozen, user_borrow_count, user_has_overdue, user_get_fine
        inst = instance_get(int(instance_id))
        if not inst:
            raise ValueError("书本实例不存在")
        if inst["status"] != 3:
            raise ValueError(f"《{inst.get('title', '?')}》不在架上，无法借阅")
        if user_is_frozen(user_id):
            raise ValueError("账号已冻结，无法借阅")
        fine = user_get_fine(user_id)
        if fine > 0:
            raise ValueError(f"有未缴罚款 ¥{fine:.1f}，请先缴纳后再借")
        if user_borrow_count(user_id) >= 5:
            raise ValueError("已达借书上限（5本），请先归还")
        if user_has_overdue(user_id):
            raise ValueError("有逾期未还的书，请先归还后再借")

    elif action == "return":
        from db import instance_borrowed_list
        borrowed = instance_borrowed_list(user_id=user_id)
        if not borrowed:
            raise ValueError("没有借阅中的书")

    task_id = str(uuid.uuid4())[:12]  # short ID for QR
    now = time.strftime("%Y-%m-%d %H:%M:%S")
    expires = time.strftime("%Y-%m-%d %H:%M:%S",
                            time.localtime(time.time() + TASK_EXPIRE_SECONDS))

    task = {
        "task_id": task_id,
        "action": action,
        "user_id": user_id,
        "payload": payload,
        "status": "pending",
        "result": {},
        "created_at": now,
        "expires_at": expires,
        "completed_at": "",
        "qr_content": json.dumps({"t": task_id, "a": action, "ts": int(time.time())},
                                 ensure_ascii=False),
    }

    with _task_lock:
        _tasks[task_id] = task
        _persist_task(task)

    logger.info(f"Task created: {task_id} action={action} user={user_id}")
    return task


def task_get(task_id: str) -> dict | None:
    """Get task by ID. Auto-checks expiry."""
    with _task_lock:
        task = _tasks.get(task_id)

    if task is None:
        # Try loading from DB
        task = _task_load_from_db(task_id)

    if task is None:
        return None

    # Check expiry
    if task["status"] == "pending":
        try:
            exp = time.mktime(time.strptime(task["expires_at"], "%Y-%m-%d %H:%M:%S"))
            if time.time() > exp:
                task["status"] = "expired"
                task["result"] = {"error": "task_expired", "message": "任务已过期，请重新生成"}
                with _task_lock:
                    _persist_task(task)
        except Exception:
            pass

    return task


def task_complete(task_id: str, result: dict):
    """Mark task as done with result."""
    now = time.strftime("%Y-%m-%d %H:%M:%S")
    with _task_lock:
        task = _tasks.get(task_id)
        if task:
            task["status"] = "done"
            task["result"] = result
            task["completed_at"] = now
            _persist_task(task)


def task_fail(task_id: str, error: str):
    """Mark task as failed with error message."""
    now = time.strftime("%Y-%m-%d %H:%M:%S")
    with _task_lock:
        task = _tasks.get(task_id)
        if task:
            task["status"] = "failed"
            task["result"] = {"error": error}
            task["completed_at"] = now
            _persist_task(task)


def task_claim(task_id: str) -> dict | None:
    """Atomically claim a pending task for execution (pending→processing).
    Returns the task dict if successfully claimed, None if already claimed/expired."""
    with _task_lock:
        task = _tasks.get(task_id)
        if task is None:
            task = _task_load_from_db(task_id)
            if task:
                _tasks[task_id] = task

        if task is None:
            return None
        if task["status"] != "pending":
            return None

        # Check expiry
        try:
            exp = time.mktime(time.strptime(task["expires_at"], "%Y-%m-%d %H:%M:%S"))
            if time.time() > exp:
                task["status"] = "expired"
                return None
        except Exception:
            pass

        task["status"] = "processing"
        _persist_task(task)
        return dict(task)


def task_cleanup():
    """Remove expired tasks from memory (keep in DB for audit)."""
    with _task_lock:
        expired = []
        for tid, task in list(_tasks.items()):
            try:
                exp = time.mktime(time.strptime(task["expires_at"], "%Y-%m-%d %H:%M:%S"))
                if time.time() > exp and task["status"] in ("pending",):
                    task["status"] = "expired"
                    _persist_task(task)
            except Exception:
                pass
            # Remove from memory if done/failed/expired older than 10 min
            if task["status"] in ("done", "failed", "expired"):
                try:
                    ct = task.get("completed_at") or task["expires_at"]
                    ct_ts = time.mktime(time.strptime(ct, "%Y-%m-%d %H:%M:%S"))
                    if time.time() - ct_ts > 600:  # 10 min
                        expired.append(tid)
                except Exception:
                    pass
        for tid in expired:
            del _tasks[tid]


def task_list(status: str = None) -> list[dict]:
    """List recent tasks for monitoring. Returns list of task dicts (without DB load)."""
    with _task_lock:
        tasks = list(_tasks.values())
    if status:
        tasks = [t for t in tasks if t["status"] == status]
    tasks.sort(key=lambda t: t["created_at"], reverse=True)
    return tasks[:50]


def _task_load_from_db(task_id: str) -> dict | None:
    """Load a task from SQLite."""
    if not _DB_AVAILABLE:
        return None
    try:
        conn = _db_module.get_db()
        row = conn.execute("SELECT * FROM task WHERE task_id=?", (task_id,)).fetchone()
        if row:
            task = dict(row)
            task["payload"] = json.loads(task.get("payload", "{}"))
            task["result"] = json.loads(task.get("result", "{}"))
            task["qr_content"] = json.dumps(
                {"t": task["task_id"], "a": task["action"], "ts": int(time.time())},
                ensure_ascii=False)
            return task
    except Exception:
        pass
    return None


# ==================== Background Cleanup ====================

_cleanup_running = False


def _cleanup_loop():
    while _cleanup_running:
        time.sleep(TASK_CLEANUP_INTERVAL)
        if _cleanup_running:
            try:
                task_cleanup()
            except Exception as e:
                logger.warning(f"Task cleanup error: {e}")


def start_cleanup():
    global _cleanup_running
    if _cleanup_running:
        return
    _cleanup_running = True
    t = threading.Thread(target=_cleanup_loop, name="task-cleanup", daemon=True)
    t.start()
    _ensure_task_table()
    logger.info("Task cleanup started")


def stop_cleanup():
    global _cleanup_running
    _cleanup_running = False


# ==================== Test ====================

if __name__ == "__main__":
    logging.basicConfig(level=logging.DEBUG)
    import sys
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

    import db
    db.DB_PATH = "/tmp/test_book.db"
    if os.path.exists(db.DB_PATH):
        os.remove(db.DB_PATH)
    db.init_db()

    start_cleanup()

    # Test create
    t = task_create("borrow", 1, {"instanceId": 123})
    print(f"Created: {t['task_id']} status={t['status']}")
    print(f"QR content: {t['qr_content']}")

    # Test claim
    claimed = task_claim(t["task_id"])
    print(f"Claimed: {claimed['status'] if claimed else 'None'}")

    # Test double-claim
    claimed2 = task_claim(t["task_id"])
    print(f"Double claim: {claimed2}")

    # Test complete
    task_complete(t["task_id"], {"ok": True, "message": "借书成功"})
    final = task_get(t["task_id"])
    print(f"Final: status={final['status']} result={final['result']}")

    stop_cleanup()
    print("All tests passed!")
