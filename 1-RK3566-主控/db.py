"""
SQLite database layer for book management system.
Replaces MySQL + MyBatis-Plus with native sqlite3.
"""
import os
import time
import sqlite3
import threading
from datetime import datetime, timedelta


class InstanceStatus:
    PENDING_STICKER = 2   # 已废弃，等同 PENDING_SHELF
    BORROWED = 1          # 已借出
    PENDING_SHELF = 2     # 未上架（待管理员上架）
    ON_SHELF = 3          # 在架


class UserRole:
    USER = 0              # 普通用户
    ADMIN = 1             # 管理员


BORROW_LIMIT = 5          # 每人最多同时借阅本数
BORROW_DAYS = 30          # 借阅期限（天）
OVERDUE_FINE_PER_DAY = 0.5  # 逾期罚款（元/天）

DB_PATH = "/userdata/book_app/book.db"

_local = threading.local()
_seq = 0
_machine_id = int.from_bytes(os.urandom(2), "big") & 0x3FF


def get_db() -> sqlite3.Connection:
    """Get thread-local DB connection with WAL mode."""
    conn = getattr(_local, "conn", None)
    if conn is None:
        conn = sqlite3.connect(DB_PATH, check_same_thread=False)
        conn.row_factory = sqlite3.Row
        conn.execute("PRAGMA journal_mode=WAL")
        conn.execute("PRAGMA foreign_keys=ON")
        _local.conn = conn
    return conn


def generate_id() -> int:
    """Simple snowflake-like ID: 42-bit ms + 10-bit machine + 12-bit sequence."""
    global _seq
    _seq = (_seq + 1) & 0xFFF
    ms = int(time.time() * 1000) & 0x3FFFFFFFFFF  # 42 bits
    return (ms << 22) | (_machine_id << 12) | _seq


def now_str() -> str:
    return datetime.now().strftime("%Y-%m-%d %H:%M:%S")


# ==================== Schema ====================

SCHEMA = """
CREATE TABLE IF NOT EXISTS book_info (
    id          INTEGER PRIMARY KEY,
    title       TEXT    NOT NULL,
    author      TEXT    DEFAULT '',
    category    TEXT    DEFAULT '',
    cover_url   TEXT    DEFAULT '',
    description TEXT    DEFAULT '',
    create_time TEXT    NOT NULL,
    update_time TEXT    NOT NULL,
    is_deleted  INTEGER DEFAULT 0
);

CREATE TABLE IF NOT EXISTS book_instance (
    instance_id INTEGER PRIMARY KEY,
    book_id     INTEGER NOT NULL,
    user_id     INTEGER DEFAULT 0,
    slot_id     INTEGER DEFAULT 0,
    status      INTEGER DEFAULT 0,
    borrower_id INTEGER DEFAULT 0,
    borrow_time TEXT    DEFAULT '',
    return_time TEXT    DEFAULT '',
    create_time TEXT    NOT NULL,
    update_time TEXT    NOT NULL,
    is_deleted  INTEGER DEFAULT 0,
    FOREIGN KEY (book_id) REFERENCES book_info(id)
);

CREATE TABLE IF NOT EXISTS storage_slot (
    slot_id     INTEGER PRIMARY KEY AUTOINCREMENT,
    coord_x     INTEGER NOT NULL,
    coord_y     INTEGER NOT NULL,
    is_occupied INTEGER DEFAULT 0,
    remark      TEXT    DEFAULT '',
    create_time TEXT    NOT NULL,
    update_time TEXT    NOT NULL,
    is_deleted  INTEGER DEFAULT 0
);

CREATE TABLE IF NOT EXISTS operation_log (
    log_id      INTEGER PRIMARY KEY AUTOINCREMENT,
    instance_id INTEGER DEFAULT 0,
    user_id     INTEGER DEFAULT 0,
    action_type TEXT    DEFAULT '',
    content     TEXT    DEFAULT '',
    operator_name TEXT  DEFAULT '',
    status      INTEGER DEFAULT 1,
    create_time TEXT    NOT NULL,
    update_time TEXT    NOT NULL,
    is_deleted  INTEGER DEFAULT 0
);

CREATE TABLE IF NOT EXISTS user (
    user_id     INTEGER PRIMARY KEY AUTOINCREMENT,
    openid      TEXT    DEFAULT '',
    nickname    TEXT    DEFAULT '',
    avatar_url  TEXT    DEFAULT '',
    role        INTEGER DEFAULT 0,
    create_time TEXT    NOT NULL,
    update_time TEXT    NOT NULL,
    is_deleted  INTEGER DEFAULT 0
);
"""

# 16 slots: coord_x=layer(1/2), coord_y=A=11-14 B=15-18
# Slot id 1-16 correspond to physical positions
DEFAULT_SLOTS = [
    # Layer 1, Zone A: 文学区 (slots 1-4, coords 11-14)
    (1, 11, "文学区-1"),
    (1, 12, "文学区-2"),
    (1, 13, "文学区-3"),
    (1, 14, "文学区-4"),
    # Layer 1, Zone B: 教辅区 (slots 5-8, coords 15-18)
    (1, 15, "教辅区-1"),
    (1, 16, "教辅区-2"),
    (1, 17, "教辅区-3"),
    (1, 18, "教辅区-4"),
    # Layer 2, Zone A: 科技区 (slots 9-12, coords 21-24)
    (2, 21, "科技区-1"),
    (2, 22, "科技区-2"),
    (2, 23, "科技区-3"),
    (2, 24, "科技区-4"),
    # Layer 2, Zone B: 其他区 (slots 13-16, coords 25-28)
    (2, 25, "其他区-1"),
    (2, 26, "其他区-2"),
    (2, 27, "其他区-3"),
    (2, 28, "其他区-4"),
]

CATEGORY_REMARK_PREFIX = {
    "教辅": "教辅区",
    "文学": "文学区",
    "科技": "科技区",
    "其他": "其他区",
}


def init_db():
    """Create tables, run migrations, and seed default data."""
    db = get_db()
    db.executescript(SCHEMA)
    db.commit()
    _migrate(db)

    # Check if slots already exist
    cur = db.execute("SELECT COUNT(*) FROM storage_slot WHERE is_deleted=0")
    if cur.fetchone()[0] == 0:
        ts = now_str()
        for coord_x, coord_y, remark in DEFAULT_SLOTS:
            db.execute(
                "INSERT INTO storage_slot (coord_x, coord_y, remark, create_time, update_time) "
                "VALUES (?, ?, ?, ?, ?)",
                (coord_x, coord_y, remark, ts, ts),
            )
        db.commit()


def _migrate(db):
    """Add missing columns to existing tables (safe to call repeatedly)."""
    migrations = [
        ("book_instance", "borrower_id", "INTEGER DEFAULT 0"),
        ("book_instance", "borrow_time", "TEXT DEFAULT ''"),
        ("book_instance", "return_time", "TEXT DEFAULT ''"),
        ("book_instance", "due_date", "TEXT DEFAULT ''"),
        ("user", "is_frozen", "INTEGER DEFAULT 0"),
        ("user", "fine_balance", "REAL DEFAULT 0.0"),
        ("operation_log", "operator_name", "TEXT DEFAULT ''"),
    ]
    for table, col, col_def in migrations:
        try:
            db.execute(f"ALTER TABLE {table} ADD COLUMN {col} {col_def}")
        except sqlite3.OperationalError:
            pass  # column already exists
    db.commit()


# ==================== Book Info CRUD ====================

def book_create(title, category="", cover_url="", description="") -> dict:
    """Create a new book record. Returns the created book dict."""
    db = get_db()
    bid = generate_id()
    ts = now_str()
    db.execute(
        "INSERT INTO book_info (id, title, author, category, cover_url, description, "
        "create_time, update_time) VALUES (?, ?, ?, ?, ?, ?, ?, ?)",
        (bid, title, "", category, cover_url, description, ts, ts),
    )
    db.commit()
    return book_get(bid)


def book_get(book_id: int) -> dict | None:
    row = get_db().execute(
        "SELECT * FROM book_info WHERE id=? AND is_deleted=0", (book_id,)
    ).fetchone()
    return dict(row) if row else None


def book_update(book_id: int, **kwargs) -> dict | None:
    allowed = {"title", "category", "cover_url", "description"}
    sets = {k: v for k, v in kwargs.items() if k in allowed}
    if not sets:
        return book_get(book_id)
    sets["update_time"] = now_str()
    cols = ", ".join(f"{k}=?" for k in sets)
    vals = list(sets.values()) + [book_id]
    get_db().execute(f"UPDATE book_info SET {cols} WHERE id=?", vals)
    get_db().commit()
    return book_get(book_id)


def book_delete(book_id: int) -> bool:
    ts = now_str()
    cur = get_db().execute(
        "UPDATE book_info SET is_deleted=1, update_time=? WHERE id=? AND is_deleted=0",
        (ts, book_id),
    )
    get_db().commit()
    return cur.rowcount > 0


def book_list(page=1, size=20, category=None) -> dict:
    db = get_db()
    where = "WHERE is_deleted=0"
    params = []
    if category:
        where += " AND category=?"
        params.append(category)
    total = db.execute(f"SELECT COUNT(*) FROM book_info {where}", params).fetchone()[0]
    offset = (page - 1) * size
    rows = db.execute(
        f"SELECT * FROM book_info {where} ORDER BY create_time DESC LIMIT ? OFFSET ?",
        params + [size, offset],
    ).fetchall()
    return {"total": total, "page": page, "size": size, "records": [dict(r) for r in rows]}


# ==================== Book Instance CRUD ====================

def instance_create(book_id: int, user_id=0, slot_id=0, status=0) -> dict:
    db = get_db()
    iid = generate_id()
    ts = now_str()
    db.execute(
        "INSERT INTO book_instance (instance_id, book_id, user_id, slot_id, status, "
        "create_time, update_time) VALUES (?, ?, ?, ?, ?, ?, ?)",
        (iid, book_id, user_id, slot_id, status, ts, ts),
    )
    db.commit()
    return instance_get(iid)


def instance_get(instance_id: int) -> dict | None:
    row = get_db().execute(
        "SELECT ins.*, bi.title, bi.category "
        "FROM book_instance ins LEFT JOIN book_info bi ON ins.book_id=bi.id "
        "WHERE ins.instance_id=? AND ins.is_deleted=0", (instance_id,)
    ).fetchone()
    return dict(row) if row else None


def instance_update(instance_id: int, **kwargs) -> dict | None:
    allowed = {"book_id", "user_id", "slot_id", "status",
               "borrower_id", "borrow_time", "return_time", "due_date"}
    sets = {k: v for k, v in kwargs.items() if k in allowed}
    if not sets:
        return instance_get(instance_id)
    sets["update_time"] = now_str()
    cols = ", ".join(f"{k}=?" for k in sets)
    vals = list(sets.values()) + [instance_id]
    get_db().execute(f"UPDATE book_instance SET {cols} WHERE instance_id=?", vals)
    get_db().commit()
    return instance_get(instance_id)


def instance_delete(instance_id: int) -> bool:
    ts = now_str()
    cur = get_db().execute(
        "UPDATE book_instance SET is_deleted=1, update_time=? "
        "WHERE instance_id=? AND is_deleted=0",
        (ts, instance_id),
    )
    get_db().commit()
    return cur.rowcount > 0


def instance_page(page=1, size=20, status=None) -> dict:
    db = get_db()
    where = "WHERE ins.is_deleted=0"
    params = []
    if status == "overdue":
        where += " AND ins.status=? AND ins.due_date!='' AND ins.due_date<?"
        params.extend([InstanceStatus.BORROWED, now_str()])
    elif status is not None:
        # Support comma-separated status like "0,2" (pending sticker + pending shelf)
        if isinstance(status, str) and "," in status:
            ids = [int(s.strip()) for s in status.split(",") if s.strip()]
            placeholders = ",".join("?" * len(ids))
            where += f" AND ins.status IN ({placeholders})"
            params.extend(ids)
        else:
            where += " AND ins.status=?"
            params.append(int(status))
    total = db.execute(
        f"SELECT COUNT(*) FROM book_instance ins {where}", params
    ).fetchone()[0]
    offset = (page - 1) * size
    rows = db.execute(
        f"SELECT ins.*, bi.title, bi.category, "
        f"u.nickname as borrower_name "
        f"FROM book_instance ins "
        f"LEFT JOIN book_info bi ON ins.book_id=bi.id "
        f"LEFT JOIN user u ON ins.borrower_id=u.user_id "
        f"{where} ORDER BY ins.create_time DESC LIMIT ? OFFSET ?",
        params + [size, offset],
    ).fetchall()
    return {"total": total, "page": page, "size": size, "records": [dict(r) for r in rows]}


# ==================== Storage Slot CRUD ====================

def slot_list() -> list[dict]:
    rows = get_db().execute(
        "SELECT * FROM storage_slot WHERE is_deleted=0 ORDER BY slot_id"
    ).fetchall()
    return [dict(r) for r in rows]


def slot_get(slot_id: int) -> dict | None:
    row = get_db().execute(
        "SELECT * FROM storage_slot WHERE slot_id=? AND is_deleted=0", (slot_id,)
    ).fetchone()
    return dict(row) if row else None


def slot_update(slot_id: int, **kwargs) -> dict | None:
    allowed = {"coord_x", "coord_y", "is_occupied", "remark"}
    sets = {k: v for k, v in kwargs.items() if k in allowed}
    if not sets:
        return slot_get(slot_id)
    sets["update_time"] = now_str()
    cols = ", ".join(f"{k}=?" for k in sets)
    vals = list(sets.values()) + [slot_id]
    get_db().execute(f"UPDATE storage_slot SET {cols} WHERE slot_id=?", vals)
    get_db().commit()
    return slot_get(slot_id)


# ==================== Core Business: Slot Allocation ====================

def scan_allocate(book_id: int, category: str, status: int = 3) -> dict:
    """
    Allocate a free storage slot for a book instance.
    Priority: same-category slots first, then overflow to any free slot.
    Uses conditional UPDATE to prevent concurrent double-allocation.

    Returns dict with slot allocation info or raises RuntimeError.
    """
    db = get_db()

    # 1. Try same-category slot first
    prefix = CATEGORY_REMARK_PREFIX.get(category, "")
    slot = None

    if prefix:
        slot = db.execute(
            "SELECT * FROM storage_slot WHERE is_occupied=0 AND is_deleted=0 "
            "AND remark LIKE ? ORDER BY slot_id LIMIT 1",
            (prefix + "%",),
        ).fetchone()

    # 2. Overflow: any free slot
    if not slot:
        slot = db.execute(
            "SELECT * FROM storage_slot WHERE is_occupied=0 AND is_deleted=0 "
            "ORDER BY slot_id LIMIT 1",
        ).fetchone()

    if not slot:
        raise RuntimeError("无空闲槽位")

    slot = dict(slot)

    # 3. Conditional UPDATE — atomic occupancy claim
    cur = db.execute(
        "UPDATE storage_slot SET is_occupied=1, update_time=? "
        "WHERE slot_id=? AND is_occupied=0",
        (now_str(), slot["slot_id"]),
    )
    if cur.rowcount == 0:
        raise RuntimeError("槽位已被占用，请重试")
    db.commit()

    # 4. Update book instance with slot_id + status
    inst = db.execute(
        "SELECT * FROM book_instance WHERE book_id=? AND is_deleted=0 "
        "ORDER BY create_time DESC LIMIT 1",
        (book_id,),
    ).fetchone()

    if not inst:
        # Rollback slot
        db.execute(
            "UPDATE storage_slot SET is_occupied=0, update_time=? WHERE slot_id=?",
            (now_str(), slot["slot_id"]),
        )
        db.commit()
        raise RuntimeError("未找到书本实例")

    inst = dict(inst)
    ts = now_str()
    db.execute(
        "UPDATE book_instance SET slot_id=?, status=?, update_time=? "
        "WHERE instance_id=?",
        (slot["slot_id"], status, ts, inst["instance_id"]),
    )
    db.commit()

    return {
        "instanceId": inst["instance_id"],
        "bookId": book_id,
        "slotId": slot["slot_id"],
        "coordX": slot["coord_x"],
        "coordY": slot["coord_y"],
        "remark": slot["remark"],
        "status": status,
    }


def slot_release(slot_id: int) -> bool:
    """Release a slot (mark as unoccupied)."""
    cur = get_db().execute(
        "UPDATE storage_slot SET is_occupied=0, update_time=? WHERE slot_id=?",
        (now_str(), slot_id),
    )
    get_db().commit()
    return cur.rowcount > 0


# ==================== Book Search ====================

def book_search(keyword: str) -> list[dict]:
    """Fuzzy search books by title."""
    db = get_db()
    like = f"%{keyword}%"
    rows = db.execute(
        "SELECT * FROM book_info WHERE is_deleted=0 AND title LIKE ? "
        "ORDER BY create_time DESC LIMIT 50",
        (like,),
    ).fetchall()
    return [dict(r) for r in rows]


# ==================== Shelf Browsing ====================

def instance_shelf_list() -> list[dict]:
    """All on-shelf instances with book info joined."""
    db = get_db()
    rows = db.execute(
        "SELECT bi.id as book_id, bi.title, bi.category, "
        "ins.instance_id, ins.slot_id, ins.status, ins.borrower_id, "
        "sl.coord_x, sl.coord_y, sl.remark "
        "FROM book_instance ins "
        "JOIN book_info bi ON ins.book_id = bi.id "
        "JOIN storage_slot sl ON ins.slot_id = sl.slot_id "
        "WHERE ins.is_deleted=0 AND ins.status=? "
        "ORDER BY sl.slot_id",
        (InstanceStatus.ON_SHELF,),
    ).fetchall()
    return [dict(r) for r in rows]


def instance_borrowed_list(user_id: int = 0) -> list[dict]:
    """Books currently borrowed, optionally filtered by user. Includes due_date."""
    db = get_db()
    if user_id:
        rows = db.execute(
            "SELECT bi.title, bi.category, "
            "ins.instance_id, ins.borrower_id, ins.borrow_time, ins.due_date "
            "FROM book_instance ins "
            "JOIN book_info bi ON ins.book_id = bi.id "
            "WHERE ins.is_deleted=0 AND ins.status=? AND ins.borrower_id=? "
            "ORDER BY ins.borrow_time DESC",
            (InstanceStatus.BORROWED, user_id),
        ).fetchall()
    else:
        rows = db.execute(
            "SELECT bi.title, bi.category, "
            "ins.instance_id, ins.borrower_id, ins.borrow_time, ins.due_date "
            "FROM book_instance ins "
            "JOIN book_info bi ON ins.book_id = bi.id "
            "WHERE ins.is_deleted=0 AND ins.status=? "
            "ORDER BY ins.borrow_time DESC",
            (InstanceStatus.BORROWED,),
        ).fetchall()
    return [dict(r) for r in rows]


# ==================== User Validation ====================

def user_borrow_count(user_id: int) -> int:
    """How many books the user currently has borrowed."""
    row = get_db().execute(
        "SELECT COUNT(*) FROM book_instance "
        "WHERE borrower_id=? AND status=? AND is_deleted=0",
        (user_id, InstanceStatus.BORROWED),
    ).fetchone()
    return row[0] if row else 0


def user_has_overdue(user_id: int) -> bool:
    """Check if user has any overdue unreturned books."""
    now = now_str()
    row = get_db().execute(
        "SELECT COUNT(*) FROM book_instance "
        "WHERE borrower_id=? AND status=? AND is_deleted=0 "
        "AND due_date != '' AND due_date < ?",
        (user_id, InstanceStatus.BORROWED, now),
    ).fetchone()
    return (row[0] if row else 0) > 0


def user_is_frozen(user_id: int) -> bool:
    """Check if user account is frozen."""
    u = user_get(user_id)
    return u is not None and u.get("is_frozen", 0) == 1


def user_add_fine(user_id: int, amount: float):
    """Add overdue fine to user balance."""
    if amount <= 0:
        return
    db = get_db()
    ts = now_str()
    db.execute(
        "UPDATE user SET fine_balance = fine_balance + ?, update_time = ? WHERE user_id = ?",
        (round(amount, 2), ts, user_id),
    )
    db.commit()


def user_get_fine(user_id: int) -> float:
    """Get user's unpaid fine balance."""
    u = user_get(user_id)
    return round(u.get("fine_balance", 0.0), 2) if u else 0.0


# ==================== Borrow / Return ====================

def borrow_instance(instance_id: int, user_id: int) -> dict:
    """
    User borrows a book: status → BORROWED, release slot, record borrower.
    Validates: not frozen, borrow limit, no overdue books.
    Sets due_date = now + BORROW_DAYS.
    Returns updated instance info or raises RuntimeError.
    """
    db = get_db()

    inst = db.execute(
        "SELECT * FROM book_instance WHERE instance_id=? AND is_deleted=0",
        (instance_id,),
    ).fetchone()
    if not inst:
        raise RuntimeError("书本实例不存在")
    inst = dict(inst)

    if inst["status"] == InstanceStatus.BORROWED:
        raise RuntimeError("该书已被借出")
    if inst["status"] == InstanceStatus.PENDING_STICKER:
        raise RuntimeError("该书尚未上架，无法借阅")

    # Validate user
    if user_is_frozen(user_id):
        raise RuntimeError("账号已冻结，无法借阅")
    count = user_borrow_count(user_id)
    if count >= BORROW_LIMIT:
        raise RuntimeError(f"已达借书上限（{BORROW_LIMIT}本），请先归还")
    if user_has_overdue(user_id):
        raise RuntimeError("有逾期未还的书，请先归还后再借")

    ts = now_str()
    due_date = (datetime.now() + timedelta(days=BORROW_DAYS)).strftime("%Y-%m-%d %H:%M:%S")

    # Release slot if occupied
    if inst["slot_id"]:
        db.execute(
            "UPDATE storage_slot SET is_occupied=0, update_time=? WHERE slot_id=?",
            (ts, inst["slot_id"]),
        )

    # Mark as borrowed, unlink slot
    db.execute(
        "UPDATE book_instance SET status=?, slot_id=0, borrower_id=?, "
        "borrow_time=?, due_date=?, return_time='', update_time=? WHERE instance_id=?",
        (InstanceStatus.BORROWED, user_id, ts, due_date, ts, instance_id),
    )
    db.commit()

    return instance_get(instance_id)


def return_instance(instance_id: int) -> dict:
    """
    User returns a book: status → PENDING_SHELF.
    Detects overdue and includes overdue_days in result.
    Returns updated instance info or raises RuntimeError.
    """
    db = get_db()

    inst = db.execute(
        "SELECT * FROM book_instance WHERE instance_id=? AND is_deleted=0",
        (instance_id,),
    ).fetchone()
    if not inst:
        raise RuntimeError("书本实例不存在")
    inst = dict(inst)

    if inst["status"] != InstanceStatus.BORROWED:
        raise RuntimeError("该书未被借出，无需归还")

    # Check overdue
    overdue_days = 0
    overdue_fine = 0.0
    if inst.get("due_date"):
        try:
            due = datetime.strptime(inst["due_date"], "%Y-%m-%d %H:%M:%S")
            overdue_days = max(0, (datetime.now() - due).days)
            overdue_fine = round(overdue_days * OVERDUE_FINE_PER_DAY, 2)
        except ValueError:
            pass

    ts = now_str()
    db.execute(
        "UPDATE book_instance SET status=?, borrower_id=0, return_time=?, "
        "update_time=? WHERE instance_id=?",
        (InstanceStatus.PENDING_SHELF, ts, ts, instance_id),
    )
    db.commit()

    result = instance_get(instance_id)
    result["overdue_days"] = overdue_days
    result["overdue_fine"] = overdue_fine
    return result


def re_shelve_instance(instance_id: int, category: str) -> dict:
    """
    Admin re-shelves a returned book: allocate slot → mechanical claw push.
    Reuses scan_allocate logic but for an existing instance.
    """
    db = get_db()

    inst = db.execute(
        "SELECT * FROM book_instance WHERE instance_id=? AND is_deleted=0",
        (instance_id,),
    ).fetchone()
    if not inst:
        raise RuntimeError("书本实例不存在")
    inst = dict(inst)

    if inst["status"] != InstanceStatus.PENDING_SHELF:
        raise RuntimeError("该书不是待上架状态")

    # Find free slot by category
    prefix = CATEGORY_REMARK_PREFIX.get(category, "")
    slot = None
    if prefix:
        slot = db.execute(
            "SELECT * FROM storage_slot WHERE is_occupied=0 AND is_deleted=0 "
            "AND remark LIKE ? ORDER BY slot_id LIMIT 1",
            (prefix + "%",),
        ).fetchone()
    if not slot:
        slot = db.execute(
            "SELECT * FROM storage_slot WHERE is_occupied=0 AND is_deleted=0 "
            "ORDER BY slot_id LIMIT 1",
        ).fetchone()
    if not slot:
        raise RuntimeError("无空闲槽位")
    slot = dict(slot)

    # Atomic claim
    ts = now_str()
    cur = db.execute(
        "UPDATE storage_slot SET is_occupied=1, update_time=? "
        "WHERE slot_id=? AND is_occupied=0",
        (ts, slot["slot_id"]),
    )
    if cur.rowcount == 0:
        raise RuntimeError("槽位已被占用，请重试")

    db.execute(
        "UPDATE book_instance SET slot_id=?, status=?, update_time=? "
        "WHERE instance_id=?",
        (slot["slot_id"], InstanceStatus.ON_SHELF, ts, instance_id),
    )
    db.commit()

    return {
        "instanceId": instance_id,
        "slotId": slot["slot_id"],
        "coordX": slot["coord_x"],
        "coordY": slot["coord_y"],
        "remark": slot["remark"],
        "status": InstanceStatus.ON_SHELF,
    }


# ==================== Instance by Slot ====================

def instance_get_by_slot(slot_id: int) -> dict | None:
    """Find the on-shelf instance occupying a given slot."""
    row = get_db().execute(
        "SELECT * FROM book_instance WHERE slot_id=? AND status=? "
        "AND is_deleted=0",
        (slot_id, InstanceStatus.ON_SHELF),
    ).fetchone()
    return dict(row) if row else None


# ==================== K230 Bridge Helpers ====================

def instance_get_on_shelf_by_book_id(book_id: int) -> dict | None:
    """Find an ON_SHELF instance for a given book_id (QR code lookup).
    Returns the most recently updated if multiple copies exist, with slot info joined."""
    row = get_db().execute(
        "SELECT ins.*, bi.title, bi.category, "
        "sl.coord_x, sl.coord_y, sl.remark "
        "FROM book_instance ins "
        "LEFT JOIN book_info bi ON ins.book_id=bi.id "
        "LEFT JOIN storage_slot sl ON ins.slot_id=sl.slot_id "
        "WHERE ins.book_id=? AND ins.status=? AND ins.is_deleted=0 "
        "ORDER BY ins.update_time DESC LIMIT 1",
        (book_id, InstanceStatus.ON_SHELF),
    ).fetchone()
    return dict(row) if row else None


def instance_get_with_slot(instance_id: int) -> dict | None:
    """Get instance with slot coord_y joined. Returns None if not found."""
    row = get_db().execute(
        "SELECT ins.*, bi.title, bi.category, "
        "sl.coord_x, sl.coord_y, sl.remark "
        "FROM book_instance ins "
        "LEFT JOIN book_info bi ON ins.book_id=bi.id "
        "LEFT JOIN storage_slot sl ON ins.slot_id=sl.slot_id "
        "WHERE ins.instance_id=? AND ins.is_deleted=0",
        (instance_id,),
    ).fetchone()
    return dict(row) if row else None


# ==================== User CRUD ====================

def user_create(openid="", nickname="", avatar_url="", role=0) -> dict:
    db = get_db()
    ts = now_str()
    cur = db.execute(
        "INSERT INTO user (openid, nickname, avatar_url, role, create_time, update_time) "
        "VALUES (?, ?, ?, ?, ?, ?)",
        (openid, nickname, avatar_url, role, ts, ts),
    )
    db.commit()
    return user_get(cur.lastrowid)


def user_get(user_id: int) -> dict | None:
    row = get_db().execute(
        "SELECT * FROM user WHERE user_id=? AND is_deleted=0", (user_id,)
    ).fetchone()
    return dict(row) if row else None


def user_get_by_openid(openid: str) -> dict | None:
    row = get_db().execute(
        "SELECT * FROM user WHERE openid=? AND is_deleted=0", (openid,)
    ).fetchone()
    return dict(row) if row else None


def user_list(page=1, size=20) -> dict:
    db = get_db()
    total = db.execute("SELECT COUNT(*) FROM user WHERE is_deleted=0").fetchone()[0]
    offset = (page - 1) * size
    rows = db.execute(
        "SELECT u.*, "
        "(SELECT COUNT(*) FROM book_instance bi "
        " WHERE bi.user_id = u.user_id AND bi.status = 1 AND bi.is_deleted = 0"
        ") AS borrow_count "
        "FROM user u WHERE u.is_deleted=0 "
        "ORDER BY u.create_time DESC LIMIT ? OFFSET ?",
        (size, offset),
    ).fetchall()
    return {"total": total, "page": page, "size": size, "records": [dict(r) for r in rows]}


def user_update(user_id: int, **kwargs) -> dict | None:
    """Update user fields (nickname, role, is_frozen, fine_balance, avatar_url)."""
    db = get_db()
    allowed = {"nickname", "avatar_url", "role", "is_frozen", "fine_balance"}
    updates = {k: v for k, v in kwargs.items() if k in allowed}
    if not updates:
        return user_get(user_id)
    updates["update_time"] = now_str()
    sets = ", ".join(f"{k}=?" for k in updates)
    db.execute(f"UPDATE user SET {sets} WHERE user_id=? AND is_deleted=0",
               (*updates.values(), user_id))
    db.commit()
    return user_get(user_id)


# ==================== Operation Log ====================

def log_create(instance_id=0, user_id=0, action_type="", content="", status=1,
               operator_name="") -> dict:
    db = get_db()
    ts = now_str()
    cur = db.execute(
        "INSERT INTO operation_log (instance_id, user_id, action_type, content, "
        "operator_name, status, create_time, update_time) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?)",
        (instance_id, user_id, action_type, content, operator_name, status, ts, ts),
    )
    db.commit()
    return log_get(cur.lastrowid)


def log_get(log_id: int) -> dict | None:
    row = get_db().execute(
        "SELECT * FROM operation_log WHERE log_id=? AND is_deleted=0", (log_id,)
    ).fetchone()
    return dict(row) if row else None


def log_page(page=1, size=20) -> dict:
    db = get_db()
    total = db.execute(
        "SELECT COUNT(*) FROM operation_log WHERE is_deleted=0"
    ).fetchone()[0]
    offset = (page - 1) * size
    rows = db.execute(
        "SELECT * FROM operation_log WHERE is_deleted=0 "
        "ORDER BY create_time DESC LIMIT ? OFFSET ?",
        (size, offset),
    ).fetchall()
    return {"total": total, "page": page, "size": size, "records": [dict(r) for r in rows]}


def log_by_instance(instance_id: int) -> list[dict]:
    """Get all operation logs for a specific book instance."""
    rows = get_db().execute(
        "SELECT * FROM operation_log WHERE instance_id=? AND is_deleted=0 "
        "ORDER BY create_time DESC",
        (instance_id,),
    ).fetchall()
    return [dict(r) for r in rows]
