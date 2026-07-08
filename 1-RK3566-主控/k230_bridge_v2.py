#!/usr/bin/env python3
"""
K230 联调桥接模块 v2 — RK3566 端 (2026-06-30)

完整协议: K230 #1 ↔ :9998 (长连接双向), RK3566 → :9999 K230 #2, K230 #2 → :9997 通知

设备角色:
  OV5695 (RK3566 本地) — 扫小程序任务 QR → 触发借/还
  K230 #1 (整书区底部) — 扫书标 QR → WiFi 发 book_id, 受 WAKE/SLEEP 控制
  K230 #2 (书架前)    — WiFi-UART 桥接 STM32 + AprilTag 导航爪子
  STM32               — 推书装置/机械爪/限位开关/机械归位

还书流程:
  用户放书→小程序QR→OV5695扫码→START_RETURN→推书→限位开关→TRIGGER
  →WAKE K230#1→扫书标→book_id校验→SLEEP→SHELVE坐标→爪子上架→DONE
  →循环直到停止码→INIT归位

借书流程:
  用户选书→小程序QR→OV5695扫码→DB借出→FETCH坐标→爪子取书→放取书区
  →FETCH_DONE→完成

协议格式 (文本行, \\n 分隔):
  // K230 #1 ↔ :9998
  BOOK:<id>[:<crc>]      — K230 #1 扫到书标
  STOP:RETURN[:<crc>]    — K230 #1 扫到停止码
  WAKE                    — RK3566 → K230 #1 唤醒
  SLEEP                   — RK3566 → K230 #1 休眠
  ACK                     — 确认
  NAK:<reason>            — 拒绝

  // RK3566 → K230 #2 :9999 (长连接)
  START_RETURN            — 启动还书推书
  SHELVE,<y>,<x>          — 上架坐标
  FETCH,<y>,<x>           — 取书坐标
  INIT                    — 机械归位
  PUSH_NEXT               — 推下一本

  // K230 #2 → RK3566 :9997 (短连接通知)
  TRIGGER                 — 限位开关触发
  DONE                    — 爪子上架完成
  FETCH_DONE              — 取书完成
  ERROR:<msg>             — 异常
"""
import socket
import threading
import time
import logging
import sys
import os
import re
import enum

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

logger = logging.getLogger("k230_bridge")

# ==================== Persistent Event Log ====================

EVENT_LOG_DIR = "/userdata/log"
EVENT_LOG_FILE = os.path.join(EVENT_LOG_DIR, "k230_events.log")
EVENT_MAX_SIZE = 1048576  # 1MB rotation
EVENT_LOG_LOCK = threading.Lock()


def _rotate_event_log():
    """Rotate event log if exceeds max size. Called at startup."""
    try:
        os.makedirs(EVENT_LOG_DIR, exist_ok=True)
        if os.path.exists(EVENT_LOG_FILE):
            sz = os.path.getsize(EVENT_LOG_FILE)
            if sz > EVENT_MAX_SIZE:
                bak = EVENT_LOG_FILE + ".old"
                if os.path.exists(bak):
                    os.remove(bak)
                os.rename(EVENT_LOG_FILE, bak)
    except Exception:
        pass


def log_event(event_type: str, **kwargs):
    """Write a structured event to the persistent log.

    Format: [ISO_TIMESTAMP] EVENT_TYPE | key=value key=value ...

    Events are persisted to /userdata/log/k230_events.log (survives reboot).
    Rotated at 1MB → .old. Thread-safe.
    """
    ts = time.strftime("%Y-%m-%d %H:%M:%S")
    parts = [f"{k}={v}" for k, v in kwargs.items()]
    line = f"[{ts}] {event_type}"
    if parts:
        line += " | " + " ".join(parts)
    line += "\n"

    with EVENT_LOG_LOCK:
        try:
            os.makedirs(EVENT_LOG_DIR, exist_ok=True)
            with open(EVENT_LOG_FILE, "a") as f:
                f.write(line)
        except Exception:
            pass  # Never let logging failure crash the bridge


# ==================== Config ====================
K2301_LISTEN_PORT = 9998       # K230 #1 ↔ RK3566 (长连接双向)
K2302_NOTIFY_PORT = 9997       # K230 #2 → RK3566 (通知接收)
K230_NAV_HOST = os.environ.get("K2302_HOST", "192.168.50.90")  # K230 #2 IP (env override)
K230_NAV_PORT = int(os.environ.get("K2302_PORT", "9999"))       # RK3566 → K230 #2
K230_NAV_TIMEOUT = float(os.environ.get("K230_NAV_TIMEOUT", "30.0"))
K230_NAV_CONNECT_TIMEOUT = float(os.environ.get("K230_CONNECT_TIMEOUT", "5.0"))
SCAN_TIMEOUT = 10.0               # 等待 K230 #1 扫码超时
TRIGGER_TIMEOUT = 30.0            # 等待限位开关触发超时
MAX_INVALID_SCANS = 3             # 连续无效扫码阈值
MAX_DUP_SCANS = 2                 # 重复扫码阈值后告警

# 书标 QR 格式
BOOK_QR_PREFIX = "BOOK:"
STOP_QR_PREFIX = "STOP:RETURN"

# ==================== Return Flow State Machine ====================

class ReturnState(enum.Enum):
    IDLE = "idle"
    RETURN_STARTED = "return_started"       # 推书装置启动中
    WAITING_TRIGGER = "waiting_trigger"     # 等待限位开关触发
    WAITING_SCAN = "waiting_scan"           # 等待 K230 #1 扫码
    VALIDATING_BOOK = "validating_book"     # 校验 book_id
    SHELVING = "shelving"                   # 爪子上架中
    RETURN_DONE = "return_done"             # 还书流程完成


class ReturnFlowManager:
    """还书流程状态机。一次还书 = 多个书本循环 + 停止码结束。"""

    def __init__(self, bridge):
        self._bridge = bridge
        self._lock = threading.Lock()
        self._state = ReturnState.IDLE
        self._user_id = None          # 触发还书的用户
        self._task_id = None          # 关联的任务 ID
        self._processed_ids = set()   # 本次循环已处理的书 ID (去重)
        self._invalid_count = 0       # 连续无效扫码计数
        self._dup_count = 0           # 连续重复扫码计数
        self._total_books = 0         # 本次还书处理的书本数
        self._total_fine = 0.0        # 累计逾期罚款
        self._trigger_event = threading.Event()
        self._scan_event = threading.Event()
        self._done_event = threading.Event()
        self._stop_event = threading.Event()
        self._scanned_book_id = None  # K230 #1 扫到的 book_id
        self._last_k2302_notify = None  # K230 #2 最近通知
        self._k2301_conn = None       # K230 #1 当前连接 (用于发 WAKE/SLEEP)
        self._k2302_conn = None       # K230 #2 长连接

    # ---- State access ----
    @property
    def state(self):
        return self._state

    @property
    def is_active(self):
        return self._state != ReturnState.IDLE

    def get_status(self) -> dict:
        with self._lock:
            return {
                "state": self._state.value,
                "user_id": self._user_id,
                "task_id": self._task_id,
                "total_books": self._total_books,
                "total_fine": round(self._total_fine, 2),
                "processed_ids": list(self._processed_ids),
                "invalid_count": self._invalid_count,
            }

    # ---- Flow Control ----

    def start_return(self, user_id: int, task_id: str = None):
        """外部调用: 开始还书流程 (OV5695 扫到还书任务 QR 后)"""
        with self._lock:
            if self._state != ReturnState.IDLE:
                logger.warning(f"[ReturnFlow] Cannot start: already in state {self._state.value}")
                return False
            self._state = ReturnState.RETURN_STARTED
            self._user_id = user_id
            self._task_id = task_id
            self._processed_ids.clear()
            self._invalid_count = 0
            self._dup_count = 0
            self._total_books = 0
            self._total_fine = 0.0
            self._trigger_event.clear()
            self._scan_event.clear()
            self._done_event.clear()
            self._stop_event.clear()
            self._scanned_book_id = None

        log_event("RETURN_START", user_id=str(user_id), task_id=str(task_id or ""))
        logger.info(f"[ReturnFlow] Started: user={user_id} task={task_id}")
        # 在线程中执行状态机
        threading.Thread(target=self._run_return_flow, name="return-flow", daemon=True).start()
        return True

    def start_admin_shelve(self, admin_user_id: int = 0) -> bool:
        """外部调用: 管理员批量上架 (整书区推书 → K230#1 扫码 → 循环上架)
        与还书流程使用相同的机械动作，但验证逻辑不同:
        - 还书: 验证 book 状态=已借出(1) → 状态变为在架(3)
        - 上架: 验证 book 状态=未上架(2) → 状态变为在架(3)
        """
        with self._lock:
            if self._state != ReturnState.IDLE:
                logger.warning(f"[AdminShelve] Cannot start: already in state {self._state.value}")
                return False
            self._state = ReturnState.RETURN_STARTED
            self._user_id = admin_user_id
            self._task_id = None
            self._processed_ids.clear()
            self._invalid_count = 0
            self._dup_count = 0
            self._total_books = 0
            self._total_fine = 0.0
            self._trigger_event.clear()
            self._scan_event.clear()
            self._done_event.clear()
            self._stop_event.clear()
            self._scanned_book_id = None

        log_event("ADMIN_SHELVE_START", admin=str(admin_user_id))
        logger.info(f"[AdminShelve] Started: admin={admin_user_id}")
        threading.Thread(target=self._run_admin_shelve_flow, name="admin-shelve-flow", daemon=True).start()
        return True

    def cancel_return(self, reason: str = "manual"):
        """外部调用: 取消还书流程"""
        with self._lock:
            if self._state == ReturnState.IDLE:
                return
            old_state = self._state
            self._state = ReturnState.IDLE
            self._trigger_event.set()
            self._scan_event.set()
            self._done_event.set()
            self._stop_event.set()
        logger.warning(f"[ReturnFlow] Cancelled from {old_state.value}: {reason}")
        self._send_init()  # 尝试让机械归位

    def notify_from_k2302(self, msg: str):
        """K230 #2 通知 (从 :9997 收到)"""
        with self._lock:
            self._last_k2302_notify = msg

        msg_upper = msg.strip().upper()
        log_event("K2302_NOTIFY", msg=msg[:80])
        if msg_upper == "TRIGGER":
            self._trigger_event.set()
        elif msg_upper == "DONE":
            self._done_event.set()
        elif msg_upper == "FETCH_DONE":
            self._done_event.set()
        elif msg_upper.startswith("ERROR:"):
            log_event("K2302_NOTIFY_ERROR", msg=msg[:80])
            logger.error(f"[ReturnFlow] K230 #2 error: {msg}")

    def notify_scan(self, book_id: int, raw_text: str):
        """K230 #1 扫到书标"""
        with self._lock:
            self._scanned_book_id = book_id
        self._scan_event.set()
        logger.info(f"[ReturnFlow] Scanned book_id={book_id} raw={raw_text}")

    def set_k2301_connection(self, conn):
        """记录 K230 #1 连接 (用于发 WAKE/SLEEP)"""
        self._k2301_conn = conn

    # ---- Internal State Machine ----

    def _send_to_k2301(self, msg: str) -> bool:
        """发送指令给 K230 #1 (通过已建立的长连接)"""
        conn = self._k2301_conn
        if conn is None:
            log_event("K2301_CMD_FAIL", cmd=msg, reason="not_connected")
            logger.error("[ReturnFlow] No K230 #1 connection")
            return False
        try:
            conn.sendall((msg + "\n").encode("ascii"))
            log_event("K2301_CMD", cmd=msg)
            return True
        except Exception as e:
            log_event("K2301_CMD_FAIL", cmd=msg, reason=str(e)[:60])
            logger.error(f"[ReturnFlow] Send to K230 #1 failed: {e}")
            self._k2301_conn = None
            return False

    def _send_to_k2302(self, msg: str) -> dict:
        """发送指令给 K230 #2，返回结果"""
        return self._bridge.send_to_k2302(msg)

    def _send_start_return(self) -> bool:
        """发 START_RETURN 给 K230 #2"""
        result = self._send_to_k2302("START_RETURN")
        return result.get("ok", False)

    def _send_shelve(self, y: int, x: int) -> bool:
        """发上架坐标给 K230 #2"""
        result = self._send_to_k2302(f"SHELVE,{y},{x}")
        return result.get("ok", False)

    def _send_push_next(self) -> bool:
        """发推下一本给 K230 #2"""
        result = self._send_to_k2302("PUSH_NEXT")
        return result.get("ok", False)

    def _send_init(self) -> bool:
        """发 INIT 给 K230 #2"""
        result = self._send_to_k2302("INIT")
        return result.get("ok", False)

    def _validate_book(self, book_id: int) -> dict:
        """校验 book_id 是否合法待还书。返回 {ok, error, instance_data}"""
        with self._lock:
            if book_id in self._processed_ids:
                self._dup_count += 1
                if self._dup_count > MAX_DUP_SCANS:
                    logger.warning(f"[ReturnFlow] Dup scan threshold exceeded for {book_id}")
                log_event("BOOK_VALIDATE", book_id=str(book_id), result="duplicate",
                          dup_count=str(self._dup_count))
                return {"ok": False, "error": "duplicate", "message": f"book_id={book_id} 本次循环已处理"}

        from db import instance_get, book_get

        inst = instance_get(book_id)
        if not inst:
            # 尝试作为 book_id 查找
            from db import instance_get_on_shelf_by_book_id
            inst = instance_get_on_shelf_by_book_id(book_id)
            if not inst:
                self._invalid_count += 1
                log_event("BOOK_VALIDATE", book_id=str(book_id), result="not_found",
                          invalid_total=str(self._invalid_count))
                return {"ok": False, "error": "not_found",
                        "message": f"book_id={book_id} 在库中不存在"}

        status = inst.get("status", -1)
        book = book_get(inst.get("book_id")) if inst.get("book_id") else None
        title = book.get("title", "?") if book else "?"

        if status != 1:  # 1 = BORROWED
            self._invalid_count += 1
            status_names = {2: "未上架", 3: "在架"}
            log_event("BOOK_VALIDATE", book_id=str(book_id), result="wrong_status",
                      status=str(status), title=title[:30],
                      invalid_total=str(self._invalid_count))
            return {"ok": False, "error": "wrong_status",
                    "message": f"《{title}》状态={status_names.get(status, status)}，不是已借出"}

        # 合法待还书
        with self._lock:
            self._invalid_count = 0
            self._dup_count = 0
            self._processed_ids.add(book_id)

        log_event("BOOK_VALIDATE", book_id=str(book_id), result="ok",
                  title=title[:40], status="BORROWED")
        return {"ok": True, "instance": inst, "book": book, "title": title}

    def _do_return_db(self, instance_id: int) -> dict:
        """执行还书 DB 操作: 借出→在架, 分配新槽位, 计算罚款"""
        from db import return_instance, instance_get, book_get, user_add_fine

        result = return_instance(instance_id)
        fine = result.get("overdue_fine", 0)
        if fine > 0:
            user_id = self._user_id
            if user_id:
                user_add_fine(user_id, fine)
            with self._lock:
                self._total_fine += fine

        inst = instance_get(instance_id)
        book = book_get(inst.get("book_id")) if inst else None
        title = book.get("title", "?") if book else "?"
        coord_y = inst.get("coord_y") if inst else None

        with self._lock:
            self._total_books += 1

        logger.info(f"[ReturnFlow] Book processed: 《{title}》 fine=¥{fine:.1f}")
        return {"ok": True, "title": title, "coord_y": coord_y, "fine": fine}

    def _run_return_flow(self):
        """还书状态机主循环 (在后台线程中运行)"""
        logger.info("[ReturnFlow] State machine started")

        try:
            # === Step 1: 启动还书 ===
            if not self._send_start_return():
                logger.error("[ReturnFlow] START_RETURN failed, aborting")
                self._end_return(False, "K230 #2 未就绪")
                return

            # === Main loop: 逐本书处理 ===
            while self._state not in (ReturnState.IDLE, ReturnState.RETURN_DONE):
                # === Step 2: 等待限位开关触发 ===
                with self._lock:
                    self._state = ReturnState.WAITING_TRIGGER
                log_event("RETURN_STATE", state="waiting_trigger")
                self._trigger_event.clear()

                logger.info("[ReturnFlow] Waiting for limit switch trigger...")
                if not self._trigger_event.wait(timeout=TRIGGER_TIMEOUT):
                    log_event("RETURN_TRIGGER_TIMEOUT", timeout=str(TRIGGER_TIMEOUT))
                    # 超时 → 可能是所有书已处理完？发 PUSH_NEXT 重试
                    logger.warning("[ReturnFlow] Trigger timeout, retrying PUSH_NEXT...")
                    if not self._send_push_next():
                        # K230 #2 不响应，可能所有书已清空
                        logger.info("[ReturnFlow] K230 #2 unresponsive, assuming done")
                        break
                    continue  # 重试等待触发

                trigger_msg = self._last_k2302_notify or "TRIGGER"

                # === Step 3: 唤醒 K230 #1 扫码 ===
                with self._lock:
                    self._state = ReturnState.WAITING_SCAN
                log_event("RETURN_STATE", state="waiting_scan")
                self._scan_event.clear()
                self._send_to_k2301("WAKE")

                logger.info("[ReturnFlow] Waiting for K230 #1 scan...")
                if not self._scan_event.wait(timeout=SCAN_TIMEOUT):
                    log_event("RETURN_SCAN_TIMEOUT", timeout=str(SCAN_TIMEOUT))
                    logger.warning("[ReturnFlow] Scan timeout after WAKE")
                    # 重试 WAKE
                    self._send_to_k2301("WAKE")
                    if not self._scan_event.wait(timeout=SCAN_TIMEOUT):
                        logger.error("[ReturnFlow] Scan timeout after retry, skipping book")
                        self._invalid_count += 1
                        if self._invalid_count >= MAX_INVALID_SCANS:
                            logger.error("[ReturnFlow] Too many failures, aborting")
                            break
                        continue

                book_id = self._scanned_book_id

                # 检查是否是停止码 (由 bridge 层通过前缀判断后调用 stop)
                if book_id is None or book_id < 0:
                    logger.info("[ReturnFlow] Stop code detected, ending loop")
                    break

                # === Step 4: 校验 book_id ===
                with self._lock:
                    self._state = ReturnState.VALIDATING_BOOK
                log_event("RETURN_STATE", state="validating_book")

                validation = self._validate_book(book_id)
                if not validation["ok"]:
                    logger.warning(f"[ReturnFlow] Validation failed: {validation.get('message')}")
                    self._send_to_k2301(f"NAK:{validation.get('error', 'unknown')}")
                    if self._invalid_count >= MAX_INVALID_SCANS:
                        logger.error("[ReturnFlow] Invalid scan threshold exceeded, alert")
                        break
                    continue

                self._send_to_k2301("ACK")

                # === Step 5: DB 处理 ===
                instance_id = validation["instance"]["instance_id"]
                result = self._do_return_db(instance_id)
                if not result["ok"]:
                    continue

                coord_y = result.get("coord_y")
                if coord_y is None:
                    logger.error(f"[ReturnFlow] No coord for instance {instance_id}")
                    continue
                y, x = self._coord_to_apriltag(coord_y)
                if y is None:
                    continue

                # === Step 6: 休眠 K230 #1 + 上架 ===
                self._send_to_k2301("SLEEP")

                with self._lock:
                    self._state = ReturnState.SHELVING
                log_event("RETURN_STATE", state="shelving", y=str(y), x=str(x),
                          title=result.get("title", "?")[:40])
                self._done_event.clear()

                if not self._send_shelve(y, x):
                    logger.error("[ReturnFlow] SHELVE failed")
                    continue

                # === Step 7: 等待爪子完成 ===
                logger.info(f"[ReturnFlow] Shelving 《{result['title']}》 to Y={y},X={x}")
                if not self._done_event.wait(timeout=K230_NAV_TIMEOUT):
                    logger.warning(f"[ReturnFlow] Shelve timeout for 《{result['title']}》")

                # 准备下一本: 发 PUSH_NEXT
                self._send_push_next()

            # === End of loop ===
            self._end_return(True, "completed")

        except Exception as e:
            logger.error(f"[ReturnFlow] Fatal error: {e}", exc_info=True)
            self._end_return(False, str(e))

    def _end_return(self, success: bool, message: str):
        """结束还书流程，清理资源"""
        logger.info(f"[ReturnFlow] Ending: success={success} msg={message} total_books={self._total_books}")
        log_event("RETURN_END", ok=str(success), message=message[:80],
                  total_books=str(self._total_books),
                  total_fine=str(round(self._total_fine, 2)))

        # 休眠 K230 #1
        self._send_to_k2301("SLEEP")

        # 机械归位
        self._send_init()

        # 更新任务状态
        if self._task_id:
            try:
                import tasks as task_module
                if success:
                    task_module.task_complete(self._task_id, {
                        "ok": True,
                        "message": f"还书完成: {self._total_books}本" +
                                   (f" · 逾期罚款 ¥{self._total_fine:.2f}" if self._total_fine > 0 else ""),
                        "totalBooks": self._total_books,
                        "totalFine": round(self._total_fine, 2),
                    })
                else:
                    task_module.task_fail(self._task_id, message)
            except Exception as e:
                logger.warning(f"[ReturnFlow] Task update failed: {e}")

        with self._lock:
            self._state = ReturnState.IDLE
            self._user_id = None
            self._task_id = None

    # ---- Admin Shelve Flow ----

    def _run_admin_shelve_flow(self):
        """管理员批量上架状态机 (在后台线程中运行)。
        与 _run_return_flow 使用相同的机械流程 (推书→扫码→上架循环)，
        但验证和DB操作不同: 未上架 → 在架。
        """
        logger.info("[AdminShelve] State machine started")

        try:
            # Step 1: 启动推书装置
            if not self._send_start_return():
                logger.error("[AdminShelve] START_RETURN failed, aborting")
                self._end_admin_shelve(False, "K230 #2 未就绪")
                return

            # Main loop: 逐本书处理
            while self._state not in (ReturnState.IDLE, ReturnState.RETURN_DONE):
                # Step 2: 等待限位开关触发
                with self._lock:
                    self._state = ReturnState.WAITING_TRIGGER
                log_event("ADMIN_SHELVE_STATE", state="waiting_trigger")
                self._trigger_event.clear()

                logger.info("[AdminShelve] Waiting for limit switch trigger...")
                if not self._trigger_event.wait(timeout=TRIGGER_TIMEOUT):
                    log_event("ADMIN_SHELVE_TRIGGER_TIMEOUT", timeout=str(TRIGGER_TIMEOUT))
                    logger.warning("[AdminShelve] Trigger timeout, retrying PUSH_NEXT...")
                    if not self._send_push_next():
                        logger.info("[AdminShelve] K230 #2 unresponsive, assuming done")
                        break
                    continue

                # Step 3: 唤醒 K230 #1 扫码
                with self._lock:
                    self._state = ReturnState.WAITING_SCAN
                log_event("ADMIN_SHELVE_STATE", state="waiting_scan")
                self._scan_event.clear()
                self._send_to_k2301("WAKE")

                logger.info("[AdminShelve] Waiting for K230 #1 scan...")
                if not self._scan_event.wait(timeout=SCAN_TIMEOUT):
                    log_event("ADMIN_SHELVE_SCAN_TIMEOUT")
                    logger.warning("[AdminShelve] Scan timeout after WAKE")
                    self._send_to_k2301("WAKE")
                    if not self._scan_event.wait(timeout=SCAN_TIMEOUT):
                        logger.error("[AdminShelve] Scan timeout after retry, skipping book")
                        self._invalid_count += 1
                        if self._invalid_count >= MAX_INVALID_SCANS:
                            logger.error("[AdminShelve] Too many failures, aborting")
                            break
                        continue

                book_id = self._scanned_book_id

                # 检查停止码
                if book_id is None or book_id < 0:
                    logger.info("[AdminShelve] Stop code detected, ending loop")
                    break

                # Step 4: 校验 book_id (未上架)
                with self._lock:
                    self._state = ReturnState.VALIDATING_BOOK
                log_event("ADMIN_SHELVE_STATE", state="validating_book")

                validation = self._validate_book_for_admin_shelve(book_id)
                if not validation["ok"]:
                    logger.warning(f"[AdminShelve] Validation failed: {validation.get('message')}")
                    self._send_to_k2301(f"NAK:{validation.get('error', 'unknown')}")
                    if self._invalid_count >= MAX_INVALID_SCANS:
                        logger.error("[AdminShelve] Invalid scan threshold exceeded, alert")
                        break
                    continue

                self._send_to_k2301("ACK")

                # Step 5: DB 处理 (未上架 → 在架)
                instance_id = validation["instance"]["instance_id"]
                result = self._do_admin_shelve_db(instance_id)
                if not result["ok"]:
                    continue

                coord_y = result.get("coord_y")
                if coord_y is None:
                    logger.error(f"[AdminShelve] No coord for instance {instance_id}")
                    continue
                y, x = self._coord_to_apriltag(coord_y)
                if y is None:
                    continue

                # Step 6: 休眠 K230 #1 + 上架
                self._send_to_k2301("SLEEP")

                with self._lock:
                    self._state = ReturnState.SHELVING
                log_event("ADMIN_SHELVE_STATE", state="shelving", y=str(y), x=str(x),
                          title=result.get("title", "?")[:40])
                self._done_event.clear()

                if not self._send_shelve(y, x):
                    logger.error("[AdminShelve] SHELVE failed")
                    continue

                # Step 7: 等待爪子完成
                logger.info(f"[AdminShelve] Shelving 《{result['title']}》 to Y={y},X={x}")
                if not self._done_event.wait(timeout=K230_NAV_TIMEOUT):
                    logger.warning(f"[AdminShelve] Shelve timeout for 《{result['title']}》")

                # 准备下一本
                self._send_push_next()

            # End of loop
            self._end_admin_shelve(True, "completed")

        except Exception as e:
            logger.error(f"[AdminShelve] Fatal error: {e}", exc_info=True)
            self._end_admin_shelve(False, str(e))

    def _validate_book_for_admin_shelve(self, book_id: int) -> dict:
        """校验 book_id 是否可上架 (状态=未上架2)。"""
        with self._lock:
            if book_id in self._processed_ids:
                self._dup_count += 1
                log_event("ADMIN_SHELVE_VALIDATE", book_id=str(book_id), result="duplicate")
                return {"ok": False, "error": "duplicate", "message": "重复扫码"}

        from db import instance_get, book_get

        inst = instance_get(book_id)
        if not inst:
            from db import instance_get_on_shelf_by_book_id
            inst = instance_get_on_shelf_by_book_id(book_id)
            if not inst:
                self._invalid_count += 1
                log_event("ADMIN_SHELVE_VALIDATE", book_id=str(book_id), result="not_found")
                return {"ok": False, "error": "not_found", "message": "book_id 不存���"}

        status = inst.get("status", -1)
        book = book_get(inst.get("book_id")) if inst.get("book_id") else None
        title = book.get("title", "?") if book else "?"

        if status != 2:  # 未上架
            self._invalid_count += 1
            status_names = {1: "已借出", 3: "在架"}
            log_event("ADMIN_SHELVE_VALIDATE", book_id=str(book_id), result="wrong_status",
                      status=str(status), title=title[:30])
            return {"ok": False, "error": "wrong_status",
                    "message": f"《{title}》状态={status_names.get(status, status)}，无需上架"}

        with self._lock:
            self._invalid_count = 0
            self._processed_ids.add(book_id)

        log_event("ADMIN_SHELVE_VALIDATE", book_id=str(book_id), result="ok", title=title[:40])
        return {"ok": True, "instance": inst, "book": book, "title": title}

    def _do_admin_shelve_db(self, instance_id: int) -> dict:
        """执行上架 DB 操作: 分配槽位 (如需要) → 状态→在架(3)"""
        from db import instance_get, instance_update, book_get, slot_get, scan_allocate, re_shelve_instance, log_create

        inst = instance_get(instance_id)
        if not inst:
            return {"ok": False, "message": f"instance {instance_id} 不存在"}

        book = book_get(inst.get("book_id")) if inst.get("book_id") else None
        title = book.get("title", "?") if book else "?"

        try:
            # 如果已经有待上架状态和槽位，用 re_shelve；否则先分配槽位
            if inst.get("status") == 2 and inst.get("slot_id"):
                result = re_shelve_instance(instance_id, book.get("category", "") if book else "")
            else:
                category = book.get("category", "") if book else ""
                result = scan_allocate(book_id=inst.get("book_id", 0), category=category, status=3)
        except Exception as e:
            logger.error(f"[AdminShelve] DB error: {e}")
            return {"ok": False, "message": str(e)}

        inst2 = instance_get(instance_id)
        coord_y = inst2.get("coord_y") if inst2 else result.get("coordY")

        with self._lock:
            self._total_books += 1

        # 操作日志
        try:
            log_create(
                instance_id=instance_id,
                action_type="ADMIN_SHELVE",
                content=f"管理员批量上架《{title}》→ 槽位{inst2.get('slot_id', '?') if inst2 else '?'}",
                status=1,
            )
        except Exception as e:
            logger.warning(f"[AdminShelve] log_create failed: {e}")

        logger.info(f"[AdminShelve] Book shelved: 《{title}》")
        return {"ok": True, "title": title, "coord_y": coord_y}

    def _end_admin_shelve(self, success: bool, message: str):
        """结束上架流程，清理资源"""
        logger.info(f"[AdminShelve] Ending: success={success} msg={message} total={self._total_books}")
        log_event("ADMIN_SHELVE_END", ok=str(success), message=message[:80],
                  total_books=str(self._total_books))

        # 批量上架完成日志
        try:
            from db import log_create
            log_create(
                action_type="ADMIN_SHELVE_BATCH_END",
                content=f"批量上架{'完成' if success else '异常结束'}: {message} (共 {self._total_books} 本)",
                status=1 if success else 0,
            )
        except Exception as e:
            logger.warning(f"[AdminShelve] log_create failed: {e}")

        self._send_to_k2301("SLEEP")
        self._send_init()

        with self._lock:
            self._state = ReturnState.IDLE
            self._user_id = None
            self._task_id = None

    def get_admin_shelve_status(self) -> dict:
        """获取管理员上架流程当前状态 (供 HDMI 轮询)"""
        with self._lock:
            current_book = None
            if hasattr(self, '_scanned_book_id') and self._scanned_book_id:
                try:
                    from db import instance_get, book_get
                    inst = instance_get(self._scanned_book_id)
                    if inst:
                        book = book_get(inst.get("book_id"))
                        current_book = book.get("title", str(self._scanned_book_id)) if book else str(self._scanned_book_id)
                except Exception:
                    pass

            return {
                "state": self._state.value,
                "total_books": self._total_books,
                "processed": len(self._processed_ids),
                "current_book": current_book,
                "invalid_count": self._invalid_count,
            }

    @staticmethod
    def _coord_to_apriltag(coord_y: int):
        """coord_y (11-28) → (Y, X)"""
        if not (11 <= coord_y <= 28):
            return None, None
        y = coord_y // 10
        x = coord_y % 10
        return (y, x) if 1 <= y <= 2 and 0 <= x <= 8 else (None, None)


# ==================== CRC4 for QR validation (matches K230 #1) ====================
# CRC-4/ITU: poly=0x3, init=0x0, no reflect, no xor-out, zero-padded to 4 digits

def compute_crc4(data: str) -> str:
    """CRC-4/ITU: polynomial 0x3, initial value 0x0.
    Returns 4-digit zero-padded hex string (e.g. '0009').
    Matches K230 #1 CRC4 implementation.
    """
    crc = 0x0  # 4-bit CRC
    for ch in data:
        crc ^= (ord(ch) & 0xF)
        for _ in range(4):
            if crc & 0x8:
                crc = ((crc << 1) ^ 0x3) & 0xF
            else:
                crc = (crc << 1) & 0xF
    return f"{crc:04X}"


def parse_book_qr(text: str) -> dict:
    """Parse book label QR code.

    Returns:
      {"type": "book", "book_id": 12345, "crc_ok": True/False}
      {"type": "stop", "crc_ok": True/False}
      {"type": "unknown", "raw": text}
    """
    text = text.strip()

    # BOOK:<id>[:<crc>]
    if text.startswith(BOOK_QR_PREFIX):
        parts = text[len(BOOK_QR_PREFIX):].split(":")
        try:
            book_id = int(parts[0])
        except ValueError:
            return {"type": "unknown", "raw": text, "reason": "invalid_book_id"}

        crc_ok = True
        if len(parts) >= 2 and parts[1]:
            expected = compute_crc4(f"BOOK:{book_id}")
            crc_ok = (parts[1].upper() == expected)
        return {"type": "book", "book_id": book_id, "crc_ok": crc_ok}

    # STOP:RETURN[:<crc>]
    if text.startswith(STOP_QR_PREFIX):
        parts = text[len(STOP_QR_PREFIX):]
        crc_ok = True
        if parts.startswith(":") and len(parts) >= 5:
            expected = compute_crc4(STOP_QR_PREFIX)
            crc_ok = (parts[1:5].upper() == expected)
        return {"type": "stop", "crc_ok": crc_ok}

    # Legacy: raw integer book_id (backward compatible)
    try:
        book_id = int(text)
        if 0 < book_id < 10**10:
            return {"type": "book", "book_id": book_id, "crc_ok": True, "legacy": True}
    except ValueError:
        pass

    return {"type": "unknown", "raw": text}


# ==================== Bridge Core ====================

class K230Bridge:
    """K230 联调通信核心。

    管理:
      - K230 #1 长连接 (:9998)
      - K230 #2 通知接收 (:9997)
      - K230 #2 指令发送 (:9999)
      - ReturnFlowManager 状态机
    """

    def __init__(self):
        self._running = False
        self._server_thread = None
        self._notify_thread = None
        self._last_request = {}           # Must be dict, not None (line 1075 assigns to it)
        self._last_request_lock = threading.Lock()
        self._k2301_conn = None          # K230 #1 TCP socket
        self._k2301_conn_lock = threading.Lock()
        self.return_flow = ReturnFlowManager(self)

    # ---- K230 #2 Communication ----

    def send_to_k2302(self, msg: str, timeout: float = K230_NAV_TIMEOUT) -> dict:
        """Send command to K230 #2 and wait for response."""
        sock = None
        log_event("K2302_SEND", cmd=msg[:80], target=f"{K230_NAV_HOST}:{K230_NAV_PORT}")
        try:
            sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            sock.settimeout(K230_NAV_CONNECT_TIMEOUT)
            sock.connect((K230_NAV_HOST, K230_NAV_PORT))
            logger.info(f"[→K230#2] Connected, sending: {msg}")

            sock.settimeout(5.0)
            sock.sendall((msg + "\n").encode("ascii"))

            sock.settimeout(timeout)
            data = b""
            while True:
                try:
                    chunk = sock.recv(1024)
                    if not chunk:
                        break
                    data += chunk
                    if b"\n" in data:
                        break
                except socket.timeout:
                    log_event("K2302_TIMEOUT", cmd=msg[:40], timeout=str(timeout))
                    return {"ok": False, "error": "timeout",
                            "message": f"K230 #2 响应超时 ({timeout}s)"}

            resp = data.decode("ascii", errors="replace").strip()
            logger.info(f"[←K230#2] Response: {resp}")
            ok = resp.upper().startswith("OK")
            log_event("K2302_RESP", cmd=msg[:40], ok=str(ok), resp=resp[:80])

            if ok:
                return {"ok": True, "message": resp}
            else:
                return {"ok": False, "error": "k2302_error", "message": resp}

        except (socket.timeout, ConnectionRefusedError, OSError) as e:
            log_event("K2302_ERROR", cmd=msg[:40], error=type(e).__name__, detail=str(e)[:80])
            logger.error(f"[→K230#2] Error: {e}")
            return {"ok": False, "error": type(e).__name__, "message": str(e)}
        finally:
            if sock:
                try: sock.close()
                except: pass

    # ---- K230 #1 Connection (long-lived bidirectional) ----

    def _handle_k2301_connection(self, client_sock, addr):
        """Handle one K230 #1 TCP connection (long-lived, bidirectional).

        IMPORTANT: Only the REAL K230 #1 connection is persistent.
        Admin commands (ADMIN:WAKE/SLEEP) are processed and disconnected immediately,
        without replacing the K230 #1 connection.
        """
        logger.info(f"[K230#1] Connected from {addr}")
        log_event("K2301_CONNECT", addr=str(addr[0]), port=str(addr[1]))

        client_sock.settimeout(5.0)
        buf = b""

        # Read the first line to determine who this is
        try:
            while b"\n" not in buf:
                chunk = client_sock.recv(1024)
                if not chunk:
                    logger.info(f"[K230#1] {addr} disconnected before identification")
                    try: client_sock.close()
                    except: pass
                    return
                buf += chunk
        except socket.timeout:
            logger.info(f"[K230#1] {addr} timeout before identification")
            try: client_sock.close()
            except: pass
            return

        first_line, remaining = buf.split(b"\n", 1)
        first_msg = first_line.decode("ascii", errors="replace").strip()

        # Is this an admin command? Process and disconnect
        if first_msg.upper() in ("ADMIN:WAKE", "ADMIN:SLEEP"):
            logger.info(f"[Admin] {addr} → {first_msg}")
            self._process_k2301_line(first_msg, client_sock)
            try: client_sock.close()
            except: pass
            return

        # Otherwise, this IS K230 #1 — replace old connection
        with self._k2301_conn_lock:
            if self._k2301_conn:
                try: self._k2301_conn.close()
                except: pass
            self._k2301_conn = client_sock
            self.return_flow.set_k2301_connection(client_sock)

        # Process the first line as K230 #1
        if first_msg:
            self._process_k2301_line(first_msg, client_sock)

        # Continue with remaining data
        client_sock.settimeout(60.0)
        buf = remaining

        try:
            while self._running:
                try:
                    chunk = client_sock.recv(1024)
                    if not chunk:
                        logger.info("[K230#1] Disconnected")
                        break
                    buf += chunk

                    while b"\n" in buf:
                        line, buf = buf.split(b"\n", 1)
                        self._process_k2301_line(line.decode("ascii", errors="replace").strip(),
                                                  client_sock)

                except socket.timeout:
                    continue  # check _running
                except OSError:
                    break

        finally:
            with self._k2301_conn_lock:
                if self._k2301_conn is client_sock:
                    self._k2301_conn = None
                    self.return_flow.set_k2301_connection(None)
            try: client_sock.close()
            except: pass
            log_event("K2301_DISCONNECT", addr=str(addr[0]))
            logger.info("[K230#1] Session ended")

    def _process_k2301_line(self, line: str, client_sock):
        """Process one line from K230 #1."""
        if not line:
            return

        logger.info(f"[←K230#1] {line}")

        # K230 #1 handshake: READY → ACK
        if line.strip().upper() == "READY":
            log_event("K2301_READY")
            self._send_to_client(client_sock, "ACK")
            return

        # ADMIN control commands (from any connection, not just K230 #1)
        upper = line.strip().upper()
        if upper == "ADMIN:WAKE":
            log_event("ADMIN_WAKE")
            if self.wake_k2301():
                self._send_to_client(client_sock, "ACK")
            else:
                self._send_to_client(client_sock, "NAK:no_k2301_connection")
            return
        if upper == "ADMIN:SLEEP":
            log_event("ADMIN_SLEEP")
            if self.sleep_k2301():
                self._send_to_client(client_sock, "ACK")
            else:
                self._send_to_client(client_sock, "NAK:no_k2301_connection")
            return

        # Parse QR format
        parsed = parse_book_qr(line)
        log_event("K2301_MSG", raw=line[:80], parsed_type=parsed.get("type", "?"))

        if parsed["type"] == "stop":
            # 停止码: 通知 ReturnFlowManager
            log_event("STOP_CODE")
            self.return_flow.notify_scan(-1, line)  # book_id=-1 表示停止
            self._send_to_client(client_sock, "ACK")

        elif parsed["type"] == "book":
            book_id = parsed["book_id"]
            crc_status = "ok" if parsed["crc_ok"] else "mismatch"

            # CRC check
            if not parsed["crc_ok"] and not parsed.get("legacy"):
                log_event("BOOK_SCAN_REJECT", book_id=str(book_id), reason="crc_mismatch")
                logger.warning(f"[K230#1] CRC mismatch for book_id={book_id}")
                self._send_to_client(client_sock, "NAK:crc_mismatch")
                return

            log_event("BOOK_SCAN", book_id=str(book_id), crc=crc_status,
                      legacy=str(parsed.get("legacy", False)))

            # Update last request
            with self._last_request_lock:
                self._last_request["last"] = {
                    "time": time.strftime("%Y-%m-%d %H:%M:%S"),
                    "book_id": book_id,
                    "raw": line,
                }

            # If return flow is active, notify it
            if self.return_flow.is_active:
                self.return_flow.notify_scan(book_id, line)
            else:
                # Standalone mode: validate book only, no navigation
                # Navigation happens only during active flows (return/admin-shelve)
                result = self._validate_book_standalone(book_id)
                log_event("BOOK_VALIDATE_STANDALONE", book_id=str(book_id),
                          ok=str(result.get("ok", False)),
                          title=result.get("title", "?"))
                if result["ok"]:
                    self._send_to_client(client_sock, "ACK")
                else:
                    self._send_to_client(client_sock, f"NAK:{result.get('error', 'unknown')}")

        elif parsed["type"] == "unknown":
            # Check for K230 #1 status/confirmation messages
            upper = line.strip().upper()
            if upper in ("ACK", "OK"):
                # K230 #1 confirming our command (WAKE/SLEEP/etc.) — silently accept
                logger.debug(f"[K230#1] Confirmation: {line}")
                return
            if upper.startswith("NAK:"):
                log_event("K2301_NAK", reason=line[4:].strip()[:60])
                logger.warning(f"[K230#1] NAK: {line[4:]}")
                return  # No response needed
            log_event("K2301_UNKNOWN", raw=line[:80])
            logger.warning(f"[K230#1] Unknown message: {line}")
            self._send_to_client(client_sock, "NAK:unknown_format")

    def _send_to_client(self, sock, msg: str):
        """Send a line back to K230 #1."""
        try:
            sock.sendall((msg + "\n").encode("ascii"))
            logger.info(f"[→K230#1] {msg}")
        except Exception as e:
            logger.error(f"[→K230#1] Send error: {e}")

    def wake_k2301(self) -> bool:
        """Send WAKE to K230 #1."""
        with self._k2301_conn_lock:
            if self._k2301_conn:
                self._send_to_client(self._k2301_conn, "WAKE")
                return True
        logger.warning("[K230#1] Cannot WAKE: not connected")
        return False

    def sleep_k2301(self) -> bool:
        """Send SLEEP to K230 #1."""
        with self._k2301_conn_lock:
            if self._k2301_conn:
                self._send_to_client(self._k2301_conn, "SLEEP")
                return True
        return False

    # ---- Book validation only (standalone, no navigation) ----

    def _validate_book_standalone(self, book_id: int) -> dict:
        """Validate book exists and is on shelf. No navigation (Phase 1 testing).
        Used when no return/admin-shelve flow is active.
        """
        from db import instance_get, instance_get_on_shelf_by_book_id, book_get

        inst = instance_get(book_id)
        if not inst:
            inst = instance_get_on_shelf_by_book_id(book_id)

        if not inst:
            return {"ok": False, "error": "book_not_found",
                    "message": f"book_id={book_id} 在库中不存在"}

        book = book_get(inst.get("book_id")) if inst.get("book_id") else None
        title = book.get("title", "?") if book else "?"
        status = inst.get("status", -1)

        if status == 3:  # ON_SHELF
            return {"ok": True, "title": title, "status": status,
                    "message": f"《{title}》在架"}
        elif status == 1:  # BORROWED
            return {"ok": False, "error": "book_borrowed",
                    "message": f"《{title}》已借出"}
        elif status == 2:  # PENDING_SHELF
            return {"ok": False, "error": "not_on_shelf",
                    "message": f"《{title}》未上架"}
        else:
            return {"ok": False, "error": "unknown_status",
                    "message": f"《{title}》状态={status}"}

    # ---- Direct lookup + navigate (standalone, triggers K230 #2) ----

    def _direct_lookup_and_nav(self, book_id: int) -> dict:
        """Standalone mode: look up book, send coordinates to K230 #2.
        Used for returning/shelving books (not part of return flow).
        """
        result = self._lookup_book(book_id)
        if not result["ok"]:
            return result

        y, x = result["y"], result["x"]
        nav_result = self.send_to_k2302(f"SHELVE,{y},{x}")

        with self._last_request_lock:
            self._last_request["nav_result"] = nav_result

        return nav_result

    def send_fetch(self, book_id: int) -> dict:
        """Fetch a book from shelf via K230 #2 (admin unshelve).
        Looks up coordinates, sends FETCH command, waits for FETCH_DONE.
        Returns dict with ok/error/message.
        """
        result = self._lookup_book_for_fetch(book_id)
        if not result["ok"]:
            return result

        y, x = result["y"], result["x"]
        title = result.get("title", "?")
        log_event("FETCH_CMD", book_id=str(book_id), y=str(y), x=str(x), title=title[:40])
        logger.info(f"[Fetch] Sending FETCH,{y},{x} for 《{title}》")

        nav_result = self.send_to_k2302(f"FETCH,{y},{x}")
        return nav_result

    def send_shelve_book(self, book_id: int) -> dict:
        """Shelve a book via K230 #2 (admin shelve, single book).
        Looks up coordinates, sends SHELVE command.
        """
        return self._direct_lookup_and_nav(book_id)

    def _lookup_book_for_fetch(self, book_id: int) -> dict:
        """DB lookup for fetch: book must be ON_SHELF with valid coords."""
        from db import instance_get_with_slot, instance_get_on_shelf_by_book_id, slot_get

        inst = instance_get_with_slot(book_id)
        if not inst:
            inst = instance_get_on_shelf_by_book_id(book_id)

        if not inst:
            return {"ok": False, "error": "book_not_found",
                    "message": f"书本 ID={book_id} 未找到"}

        title = inst.get("title", "?")
        status = inst.get("status", -1)

        if status != 3:  # Must be ON_SHELF for fetch
            status_names = {1: "已借出", 2: "未上架", 3: "在架"}
            return {"ok": False, "error": "not_on_shelf",
                    "message": f"《{title}》({status_names.get(status, status)})不在架上"}

        coord_y = inst.get("coord_y")
        if not coord_y:
            slot_id = inst.get("slot_id", 0)
            if slot_id:
                slot = slot_get(slot_id)
                if slot:
                    coord_y = slot.get("coord_y")

        if not coord_y:
            return {"ok": False, "error": "no_coord",
                    "message": f"《{title}》无物理坐标"}

        y, x = ReturnFlowManager._coord_to_apriltag(coord_y)
        if y is None:
            return {"ok": False, "error": "invalid_coord",
                    "message": f"坐标 {coord_y} 无效"}

        return {"ok": True, "instance_id": inst["instance_id"], "title": title,
                "y": y, "x": x, "coord_y": coord_y}

    def _lookup_book_for_shelve(self, book_id: int) -> dict:
        """DB lookup for admin shelve: book must be status=2 (未上架)."""
        from db import instance_get_with_slot, instance_get, slot_get, scan_allocate

        inst = instance_get_with_slot(book_id)
        if not inst:
            inst = instance_get(book_id)

        if not inst:
            return {"ok": False, "error": "book_not_found",
                    "message": f"书本 ID={book_id} 未找到"}

        title = inst.get("title", "?")
        status = inst.get("status", -1)

        if status != 2:  # 未上架
            status_names = {1: "已借出", 2: "未上架", 3: "在架"}
            return {"ok": False, "error": "invalid_status",
                    "message": f"《{title}》({status_names.get(status, status)})无需上架"}

        # Get or allocate slot
        coord_y = inst.get("coord_y")
        slot_id = inst.get("slot_id", 0)
        if not coord_y and slot_id:
            slot = slot_get(slot_id)
            if slot:
                coord_y = slot.get("coord_y")

        if not coord_y:
            # Allocate new slot
            try:
                book_id_int = inst.get("book_id", book_id)
                category = inst.get("category", "")
                result = scan_allocate(book_id=book_id_int, category=category, status=3)
                coord_y = result.get("coordY")
                slot_id = result.get("slotId")
            except Exception as e:
                return {"ok": False, "error": "alloc_failed",
                        "message": f"分配槽位失败: {e}"}

        if not coord_y:
            return {"ok": False, "error": "no_coord",
                    "message": f"《{title}》无可用槽位"}

        y, x = ReturnFlowManager._coord_to_apriltag(coord_y)
        if y is None:
            return {"ok": False, "error": "invalid_coord",
                    "message": f"坐标 {coord_y} 无效"}

        return {"ok": True, "instance_id": inst["instance_id"], "title": title,
                "y": y, "x": x, "coord_y": coord_y}

    def _lookup_book(self, book_id: int) -> dict:
        """DB lookup for book by ID."""
        from db import instance_get_with_slot, instance_get_on_shelf_by_book_id, slot_get

        inst = instance_get_with_slot(book_id)
        if not inst:
            inst = instance_get_on_shelf_by_book_id(book_id)

        if not inst:
            return {"ok": False, "error": "book_not_found",
                    "message": f"书本 ID={book_id} 未找到"}

        title = inst.get("title", "?")
        status = inst.get("status", -1)

        if status != 3:  # ON_SHELF only for direct nav
            status_names = {1: "已借出", 2: "未上架", 3: "在架"}
            return {"ok": False, "error": "not_on_shelf",
                    "message": f"《{title}》({status_names.get(status, status)})"}

        coord_y = inst.get("coord_y")
        if not coord_y:
            slot_id = inst.get("slot_id", 0)
            if slot_id:
                slot = slot_get(slot_id)
                if slot:
                    coord_y = slot.get("coord_y")

        if not coord_y:
            return {"ok": False, "error": "no_coord",
                    "message": f"《{title}》无物理坐标"}

        y, x = ReturnFlowManager._coord_to_apriltag(coord_y)
        if y is None:
            return {"ok": False, "error": "invalid_coord",
                    "message": f"坐标 {coord_y} 无效"}

        return {"ok": True, "instance_id": inst["instance_id"], "title": title,
                "y": y, "x": x, "coord_y": coord_y}

    # ---- K230 #2 Notification Server (:9997) ----

    def _run_notify_server(self):
        """Listen on :9997 for notifications from K230 #2."""
        server_sock = None
        try:
            server_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            server_sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            server_sock.settimeout(1.0)
            server_sock.bind(("0.0.0.0", K2302_NOTIFY_PORT))
            server_sock.listen(1)
            logger.info(f"[Notify] Listening on :{K2302_NOTIFY_PORT}")

            while self._running:
                try:
                    client, addr = server_sock.accept()
                    logger.info(f"[Notify] K230 #2 connected from {addr}")
                    client.settimeout(5.0)

                    try:
                        data = client.recv(1024)
                        if data:
                            msg = data.decode("ascii", errors="replace").strip()
                            logger.info(f"[Notify] ←K230#2: {msg}")
                            self.return_flow.notify_from_k2302(msg)
                            client.sendall(b"OK\n")
                    except socket.timeout:
                        pass
                    finally:
                        try: client.close()
                        except: pass

                except socket.timeout:
                    continue
                except OSError as e:
                    if self._running:
                        logger.error(f"[Notify] Accept error: {e}")

        except OSError as e:
            logger.error(f"[Notify] Server error: {e}")
        finally:
            if server_sock:
                try: server_sock.close()
                except: pass

    # ---- Server lifecycle ----

    def _run_k2301_server(self):
        """Listen on :9998 for K230 #1 connections."""
        server_sock = None
        try:
            server_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            server_sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            server_sock.settimeout(1.0)
            server_sock.bind(("0.0.0.0", K2301_LISTEN_PORT))
            server_sock.listen(1)
            logger.info(f"[K230#1 Server] Listening on :{K2301_LISTEN_PORT}")

            while self._running:
                try:
                    client, addr = server_sock.accept()
                    # Handle each connection in a new thread (for reconnection)
                    t = threading.Thread(target=self._handle_k2301_connection,
                                         args=(client, addr), daemon=True)
                    t.start()
                except socket.timeout:
                    continue
                except OSError as e:
                    if self._running:
                        logger.error(f"[K230#1 Server] Accept error: {e}")

        except OSError as e:
            logger.error(f"[K230#1 Server] Fatal: {e}")
        finally:
            if server_sock:
                try: server_sock.close()
                except: pass

    def start(self):
        if self._running:
            return
        self._running = True

        # Start K230 #1 listener (:9998)
        self._server_thread = threading.Thread(
            target=self._run_k2301_server, name="k2301-server", daemon=True)
        self._server_thread.start()

        # Start K230 #2 notification listener (:9997)
        self._notify_thread = threading.Thread(
            target=self._run_notify_server, name="k2302-notify", daemon=True)
        self._notify_thread.start()

        # Start status file writer
        threading.Thread(target=self._status_writer_loop, name="status-writer", daemon=True).start()

        logger.info(f"[Bridge v2] Started — K230 #1 :{K2301_LISTEN_PORT} / Notify :{K2302_NOTIFY_PORT} / K230 #2 → {K230_NAV_HOST}:{K230_NAV_PORT}")

    def stop(self):
        self._running = False
        self.return_flow.cancel_return("bridge_stopping")
        logger.info("[Bridge v2] Stopped")

    def get_status(self) -> dict:
        status = {
            "running": self._running,
            "k2301_port": K2301_LISTEN_PORT,
            "k2302_host": K230_NAV_HOST,
            "k2302_port": K230_NAV_PORT,
            "notify_port": K2302_NOTIFY_PORT,
            "k2301_connected": self._k2301_conn is not None,
            "return_flow": self.return_flow.get_status(),
        }
        with self._last_request_lock:
            status["last_request"] = self._last_request.get("last")
        return status

    def _write_status_file(self):
        """Write status to /tmp/k230_bridge_status.json for Flask API."""
        import json
        try:
            with open("/tmp/k230_bridge_status.json", "w") as f:
                json.dump(self.get_status(), f)
        except Exception:
            pass

    def _status_writer_loop(self):
        """Background thread: write status file every 5 seconds."""
        while self._running:
            self._write_status_file()
            time.sleep(5)


# ==================== Singleton ====================

_bridge_instance: K230Bridge | None = None


def get_bridge() -> K230Bridge:
    global _bridge_instance
    if _bridge_instance is None:
        _bridge_instance = K230Bridge()
    return _bridge_instance


# ==================== Direct Test ====================

if __name__ == "__main__":
    logging.basicConfig(level=logging.DEBUG,
                        format="%(asctime)s [%(name)s] %(message)s")

    # Setup persistent event logging
    _rotate_event_log()
    log_event("BRIDGE_START", version="v2", pid=str(os.getpid()))

    import db
    db.DB_PATH = os.environ.get("BOOK_DB_PATH", "/userdata/book_app/book.db")
    os.makedirs(os.path.dirname(db.DB_PATH), exist_ok=True)
    db.init_db()

    bridge = get_bridge()
    bridge.start()
    log_event("BRIDGE_READY", k2301_port=str(K2301_LISTEN_PORT),
              k2302_host=str(K230_NAV_HOST), k2302_port=str(K230_NAV_PORT),
              notify_port=str(K2302_NOTIFY_PORT))
    print("=" * 60)
    print("K230 Bridge v2 — 完整借还协议")
    print(f"  K230 #1 ↔ :{K2301_LISTEN_PORT} (长连接双向)")
    print(f"  K230 #2 ← :{K2302_NOTIFY_PORT} (通知接收)")
    print(f"  K230 #2 → {K230_NAV_HOST}:{K230_NAV_PORT}")
    print()
    print("Test commands (send to :9998 from another terminal):")
    print(f"  echo 'BOOK:12345' | nc {socket.gethostname()} {K2301_LISTEN_PORT}")
    print(f"  echo 'STOP:RETURN' | nc {socket.gethostname()} {K2301_LISTEN_PORT}")
    print("=" * 60)

    try:
        while True:
            time.sleep(1)
    except KeyboardInterrupt:
        print("\nShutting down...")
        log_event("BRIDGE_STOP", reason="keyboard_interrupt")
        bridge.stop()
