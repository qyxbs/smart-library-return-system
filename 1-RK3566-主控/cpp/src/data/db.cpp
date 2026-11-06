// =============================================================================
// db.cpp — SQLite 数据层（建表、借还、槽位、日志）
// =============================================================================

#include "data/db.h"

#include <sqlite3.h>

#include <chrono>
#include <cmath>        // std::round —— 金额四舍五入到分
#include <cstdlib>      // std::getenv
#include <cstring>
#include <ctime>        // strptime / mktime / localtime_r / difftime / strftime
#include <memory>       // std::unique_ptr
#include <mutex>
#include <stdexcept>    // std::runtime_error
#include <string>
#include <thread>       // std::this_thread::get_id
#include <utility>      // std::pair

#include "common/config.h"
#include "common/log.h"

namespace db {

namespace {

class Statement {
public:
    // explicit 表示不允许隐式转换，必须显式写 Statement s(stmt)
    explicit Statement(sqlite3_stmt* stmt) : stmt_(stmt) {}

    // 析构函数：对象销毁时自动调用
    ~Statement() {
        if (stmt_ != nullptr) {
            sqlite3_finalize(stmt_);
        }
    }

    // 【禁止拷贝】语句对象代表一个唯一的资源，不能复制两份（否则会 double free）
    // = delete 是 C++11 的写法：明确告诉编译器"这个函数不存在"
    Statement(const Statement&) = delete;
    Statement& operator=(const Statement&) = delete;

    // 【允许移动】把一个语句对象"移交"给另一个变量，原来的置空
    Statement(Statement&& other) noexcept : stmt_(other.stmt_) {
        other.stmt_ = nullptr;
    }

    Statement& bind(int idx, int64_t v) {
        sqlite3_bind_int64(stmt_, idx, v);
        return *this;
    }

    Statement& bind(int idx, int v) {
        sqlite3_bind_int(stmt_, idx, v);
        return *this;
    }

    Statement& bind(int idx, double v) {
        sqlite3_bind_double(stmt_, idx, v);
        return *this;
    }

    Statement& bind(int idx, const std::string& v) {
        sqlite3_bind_text(stmt_, idx, v.c_str(), static_cast<int>(v.size()),
                          SQLITE_TRANSIENT);
        return *this;
    }

    Statement& bind_null(int idx) {
        sqlite3_bind_null(stmt_, idx);
        return *this;
    }

    bool step() {
        int rc = sqlite3_step(stmt_);
        if (rc == SQLITE_ROW) {
            return true;      // 有数据
        }
        if (rc == SQLITE_DONE) {
            return false;     // 正常结束
        }
        // 其他返回码都是错误
        throw std::runtime_error(std::string("SQL 执行失败: ") + sqlite3_errmsg(
                                     sqlite3_db_handle(stmt_)));
    }

    int64_t col_int64(int col) {
        return sqlite3_column_int64(stmt_, col);
    }

    int col_int(int col) {
        return sqlite3_column_int(stmt_, col);
    }

    double col_double(int col) {
        return sqlite3_column_double(stmt_, col);
    }

    std::string col_text(int col) {
        const unsigned char* p = sqlite3_column_text(stmt_, col);
        if (p == nullptr) {
            return std::string();   // 返回空字符串而不是崩溃
        }
        // 用长度构造：因为文本里可能有 '\0'（虽然业务上不会有，但严谨些）
        int len = sqlite3_column_bytes(stmt_, col);
        return std::string(reinterpret_cast<const char*>(p), static_cast<size_t>(len));
    }

    // 执行写操作并返回影响行数（用于判断"抢格子"是否成功）
    int exec_and_changes() {
        step();                              // 写操作调一次 step 就执行完了
        return sqlite3_changes(sqlite3_db_handle(stmt_));
    }

private:
    sqlite3_stmt* stmt_ = nullptr;
};

class Connection {
public:
    explicit Connection(const std::string& path) {
        int rc = sqlite3_open(path.c_str(), &db_);
        if (rc != SQLITE_OK) {
            std::string msg = db_ ? sqlite3_errmsg(db_) : "未知错误";
            if (db_) {
                sqlite3_close(db_);
                db_ = nullptr;
            }
            throw std::runtime_error("打开数据库失败 " + path + ": " + msg);
        }

        exec("PRAGMA journal_mode=WAL");

        // busy_timeout：数据库被别的线程/进程锁住时，等多久再报错（毫秒）
        // 设成 5000 = 等 5 秒。不设的话一遇到锁就立刻返回 SQLITE_BUSY。
        exec("PRAGMA busy_timeout=5000");

        // 外键约束默认是关闭的，必须手动打开（SQLite 为了兼容老版本）
        exec("PRAGMA foreign_keys=ON");
    }

    ~Connection() {
        if (db_ != nullptr) {
            sqlite3_close(db_);
        }
    }

    Connection(const Connection&) = delete;
    Connection& operator=(const Connection&) = delete;

    // 执行一句不关心返回值的 SQL
    void exec(const std::string& sql) {
        char* err = nullptr;
        int rc = sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &err);
        if (rc != SQLITE_OK) {
            std::string msg = err ? err : "未知错误";
            sqlite3_free(err);   // sqlite3_exec 用 malloc 分配的错误信息，要用 sqlite3_free 释放
            throw std::runtime_error("SQL 执行失败: " + msg + " (语句: " + sql + ")");
        }
    }

    // 准备一条语句
    Statement prepare(const std::string& sql) {
        sqlite3_stmt* stmt = nullptr;
        int rc = sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr);
        if (rc != SQLITE_OK) {
            throw std::runtime_error("SQL 准备失败: " +
                                     std::string(sqlite3_errmsg(db_)) +
                                     " (语句: " + sql + ")");
        }
        return Statement(stmt);
    }

    // 取最近一次 INSERT 生成的自增 ID
    int64_t last_insert_rowid() {
        return sqlite3_last_insert_rowid(db_);
    }

    sqlite3* raw() { return db_; }

private:
    sqlite3* db_ = nullptr;
};

thread_local std::unique_ptr<Connection> g_conn;

// 全局的数据库路径（所有线程共用同一个路径）
std::string g_db_path;
std::mutex g_init_mtx;   // 保护 init 的初始化过程

// 取当前线程的连接（没建过就建一个）
Connection& conn() {
    if (!g_conn) {
        g_conn = std::make_unique<Connection>(g_db_path);
    }
    return *g_conn;
}

BookInfo row_to_book(Statement& s) {
    BookInfo b;
    b.id          = s.col_int64(0);
    b.title       = s.col_text(1);
    b.author      = s.col_text(2);
    b.category    = s.col_text(3);
    b.cover_url   = s.col_text(4);
    b.description = s.col_text(5);
    b.create_time = s.col_text(6);
    b.update_time = s.col_text(7);
    b.is_deleted  = s.col_int(8);
    return b;
}

const char* INSTANCE_SELECT =
    "SELECT i.instance_id, i.book_id, i.user_id, i.slot_id, i.status, i.borrower_id, "
    "       i.borrow_time, i.return_time, "
    "       COALESCE(i.due_date,'') AS due_date, "
    "       i.create_time, i.update_time, i.is_deleted, "
    "       COALESCE(b.title,'')    AS title, "
    "       COALESCE(b.category,'') AS category, "
    "       COALESCE(s.coord_x,0)   AS coord_x, "
    "       COALESCE(s.coord_y,0)   AS coord_y, "
    "       COALESCE(s.remark,'')   AS remark, "
    "       COALESCE(u.nickname,'') AS borrower_name "
    "FROM book_instance i "
    "LEFT JOIN book_info    b ON b.id = i.book_id "
    "LEFT JOIN storage_slot s ON s.slot_id = i.slot_id "
    "LEFT JOIN user         u ON u.user_id = i.borrower_id ";

BookInstance row_to_instance(Statement& s) {
    BookInstance x;
    x.instance_id   = s.col_int64(0);
    x.book_id       = s.col_int64(1);
    x.user_id       = s.col_int64(2);
    x.slot_id       = s.col_int64(3);
    x.status        = s.col_int(4);
    x.borrower_id   = s.col_int64(5);
    x.borrow_time   = s.col_text(6);
    x.return_time   = s.col_text(7);
    x.due_date      = s.col_text(8);
    x.create_time   = s.col_text(9);
    x.update_time   = s.col_text(10);
    x.is_deleted    = s.col_int(11);
    x.title         = s.col_text(12);
    x.category      = s.col_text(13);
    x.coord_x       = s.col_int(14);
    x.coord_y       = s.col_int(15);
    x.remark        = s.col_text(16);
    x.borrower_name = s.col_text(17);
    return x;
}

StorageSlot row_to_slot(Statement& s) {
    StorageSlot sl;
    sl.slot_id     = s.col_int64(0);
    sl.coord_x     = s.col_int(1);
    sl.coord_y     = s.col_int(2);
    sl.is_occupied = s.col_int(3);
    sl.remark      = s.col_text(4);
    sl.create_time = s.col_text(5);
    sl.update_time = s.col_text(6);
    sl.is_deleted  = s.col_int(7);
    return sl;
}

User row_to_user(Statement& s) {
    User u;
    u.user_id      = s.col_int64(0);
    u.openid       = s.col_text(1);
    u.nickname     = s.col_text(2);
    u.avatar_url   = s.col_text(3);
    u.role         = s.col_int(4);
    u.is_frozen    = s.col_int(5);
    u.fine_balance = s.col_double(6);
    u.create_time  = s.col_text(7);
    u.update_time  = s.col_text(8);
    u.is_deleted   = s.col_int(9);
    return u;
}

OperationLog row_to_log(Statement& s) {
    OperationLog lg;
    lg.log_id        = s.col_int64(0);
    lg.instance_id   = s.col_int64(1);
    lg.user_id       = s.col_int64(2);
    lg.action_type   = s.col_text(3);
    lg.content       = s.col_text(4);
    lg.status        = s.col_int(5) ;
    lg.create_time   = s.col_text(6);
    lg.operator_name = s.col_text(7);
    return lg;
}

const char* SCHEMA_SQL = R"SQL(
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
    due_date    TEXT    DEFAULT '',
    create_time TEXT    NOT NULL,
    update_time TEXT    NOT NULL,
    is_deleted  INTEGER DEFAULT 0
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
    log_id        INTEGER PRIMARY KEY AUTOINCREMENT,
    instance_id   INTEGER DEFAULT 0,
    user_id       INTEGER DEFAULT 0,
    action_type   TEXT    DEFAULT '',
    content       TEXT    DEFAULT '',
    status        INTEGER DEFAULT 1,
    create_time   TEXT    NOT NULL,
    operator_name TEXT    DEFAULT ''
);

CREATE TABLE IF NOT EXISTS "user" (
    user_id      INTEGER PRIMARY KEY AUTOINCREMENT,
    openid       TEXT    DEFAULT '',
    nickname     TEXT    DEFAULT '',
    avatar_url   TEXT    DEFAULT '',
    role         INTEGER DEFAULT 0,
    is_frozen    INTEGER DEFAULT 0,
    fine_balance REAL    DEFAULT 0,
    create_time  TEXT    NOT NULL,
    update_time  TEXT    NOT NULL,
    is_deleted   INTEGER DEFAULT 0
);

CREATE TABLE IF NOT EXISTS task (
    task_id      TEXT PRIMARY KEY,
    action       TEXT NOT NULL,
    user_id      INTEGER NOT NULL,
    payload      TEXT DEFAULT '{}',
    status       TEXT DEFAULT 'pending',
    result       TEXT DEFAULT '{}',
    created_at   TEXT NOT NULL,
    expires_at   TEXT NOT NULL,
    completed_at TEXT DEFAULT ''
);
)SQL";

struct SlotInit { int coord_x; int coord_y; const char* remark; };
const SlotInit SLOT_INITS[] = {
    {1, 11, "教辅区-1"}, {1, 12, "教辅区-2"}, {1, 13, "教辅区-3"}, {1, 14, "教辅区-4"},
    {1, 15, "文学区-1"}, {1, 16, "文学区-2"}, {1, 17, "文学区-3"}, {1, 18, "文学区-4"},
    {2, 21, "科技区-1"}, {2, 22, "科技区-2"}, {2, 23, "科技区-3"}, {2, 24, "科技区-4"},
    {2, 25, "其他区-1"}, {2, 26, "其他区-2"}, {2, 27, "其他区-3"}, {2, 28, "其他区-4"},
};

// 分类 → 备注前缀（分配格子时按这个前缀找同分类区域）
const std::pair<const char*, const char*> CATEGORY_PREFIXES[] = {
    {"教辅", "教辅区"},
    {"文学", "文学区"},
    {"科技", "科技区"},
    {"其他", "其他区"},
};

}  // namespace

void init(const std::string& path) {
    std::lock_guard<std::mutex> lock(g_init_mtx);

    // 决定数据库路径，优先级：参数 > 环境变量 > 默认值
    if (!path.empty()) {
        g_db_path = path;
    } else if (const char* env = std::getenv("BOOK_DB_PATH")) {
        g_db_path = env;
    } else {
        g_db_path = cfg::DB_PATH_DEFAULT;
    }

    LOG_INFO("[DB] 数据库路径: ", g_db_path);

    Connection& c = conn();

    // 建表（IF NOT EXISTS，重复执行无害）
    c.exec(SCHEMA_SQL);
    LOG_INFO("[DB] 表结构就绪");

    {
        bool has_due_date = false;
        Statement info = c.prepare("PRAGMA table_info(book_instance)");
        while (info.step()) {
            if (info.col_text(1) == "due_date") {
                has_due_date = true;
                break;
            }
        }
        if (!has_due_date) {
            c.exec("ALTER TABLE book_instance ADD COLUMN due_date TEXT DEFAULT ''");
            LOG_INFO("[DB] 迁移：已为 book_instance 添加 due_date 列");
        }
    }

    // 灌初始格子（只在表为空时做，避免重复插入）
    Statement cnt = c.prepare("SELECT COUNT(*) FROM storage_slot");
    cnt.step();
    int64_t slot_count = cnt.col_int64(0);
    if (slot_count == 0) {
        std::string ts = now_str();
        for (const auto& si : SLOT_INITS) {
            Statement ins = c.prepare(
                "INSERT INTO storage_slot (coord_x, coord_y, is_occupied, remark, "
                "create_time, update_time, is_deleted) VALUES (?,?,0,?,?,?,0)");
            ins.bind(1, si.coord_x)
               .bind(2, si.coord_y)
               .bind(3, std::string(si.remark))
               .bind(4, ts)
               .bind(5, ts);
            ins.step();
        }
        LOG_INFO("[DB] 已初始化 ", static_cast<int>(sizeof(SLOT_INITS) / sizeof(SLOT_INITS[0])),
                 " 个书架格子");
    } else {
        LOG_INFO("[DB] 已有 ", slot_count, " 个格子，跳过初始化");
    }
}

std::string current_path() {
    return g_db_path;
}

int64_t generate_id() {
    static std::mutex mtx;
    static int64_t last_ms = 0;
    static int64_t seq = 0;

    std::lock_guard<std::mutex> lock(mtx);

    int64_t now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                         std::chrono::system_clock::now().time_since_epoch()).count();

    if (now_ms == last_ms) {
        // 同一毫秒内：序号 +1；超过 4095 就等到下一毫秒
        seq = (seq + 1) & 0xFFF;
        if (seq == 0) {
            while (now_ms <= last_ms) {
                now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                             std::chrono::system_clock::now().time_since_epoch()).count();
            }
        }
    } else {
        seq = 0;
    }
    last_ms = now_ms;

    // 41 位时间戳 + 10 位机器号 + 12 位序号
    // (1L << 22) 是为了把低 22 位让给机器号和序号
    return (now_ms << 22) | (1L << 12) | seq;
}

std::string now_str() {
    std::time_t t = std::time(nullptr);
    std::tm tm_buf{};
    localtime_r(&t, &tm_buf);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm_buf);
    return std::string(buf);
}

BookInfo book_create(const std::string& title,
                     const std::string& category,
                     const std::string& cover_url,
                     const std::string& description,
                     const std::string& author) {
    Connection& c = conn();
    std::string ts = now_str();
    int64_t id = generate_id();

    Statement s = c.prepare(
        "INSERT INTO book_info (id, title, author, category, cover_url, description, "
        "create_time, update_time, is_deleted) VALUES (?,?,?,?,?,?,?,?,0)");
    s.bind(1, id)
     .bind(2, title)
     .bind(3, author)
     .bind(4, category)
     .bind(5, cover_url)
     .bind(6, description)
     .bind(7, ts)
     .bind(8, ts);
    s.step();

    LOG_INFO("[DB] 新建书目 id=", id, " 《", title, "》 分类=",
             category.empty() ? "未分类" : category);

    BookInfo b;
    b.id = id;
    b.title = title;
    b.author = author;
    b.category = category;
    b.cover_url = cover_url;
    b.description = description;
    b.create_time = ts;
    b.update_time = ts;
    b.is_deleted = 0;
    return b;
}

std::optional<BookInfo> book_get(int64_t book_id) {
    Statement s = conn().prepare(
        "SELECT id, title, author, category, cover_url, description, "
        "create_time, update_time, is_deleted "
        "FROM book_info WHERE id=? AND is_deleted=0");
    s.bind(1, book_id);
    if (!s.step()) {
        return std::nullopt;   // 查不到
    }
    return row_to_book(s);
}

bool book_update(int64_t book_id, const BookUpdate& upd) {
    std::string sql = "UPDATE book_info SET update_time=?";
    std::vector<std::string> params;   // 按顺序记录要绑定的字符串参数

    std::string ts = now_str();

    if (upd.title)       { sql += ", title=?";       params.push_back(*upd.title); }
    if (upd.category)    { sql += ", category=?";    params.push_back(*upd.category); }
    if (upd.cover_url)   { sql += ", cover_url=?";   params.push_back(*upd.cover_url); }
    if (upd.description) { sql += ", description=?"; params.push_back(*upd.description); }
    if (upd.author)      { sql += ", author=?";      params.push_back(*upd.author); }

    if (params.empty()) {
        return false;   // 没有要改的字段
    }

    sql += " WHERE id=? AND is_deleted=0";

    Statement s = conn().prepare(sql);
    int idx = 1;
    s.bind(idx++, ts);
    for (const auto& p : params) {
        s.bind(idx++, p);
    }
    s.bind(idx++, book_id);

    return s.exec_and_changes() > 0;
}

bool book_delete(int64_t book_id) {
    // 软删除：只把 is_deleted 置 1，数据还在（以后要恢复或者做审计都用得上）
    Statement s = conn().prepare(
        "UPDATE book_info SET is_deleted=1, update_time=? WHERE id=? AND is_deleted=0");
    s.bind(1, now_str()).bind(2, book_id);
    return s.exec_and_changes() > 0;
}

PageResult<BookInfo> book_list(int page, int size,
                               const std::optional<std::string>& category) {
    if (page < 1) page = 1;
    if (size < 1) size = 20;

    PageResult<BookInfo> result;
    result.page = page;
    result.size = size;

    Connection& c = conn();

    std::string count_sql = "SELECT COUNT(*) FROM book_info WHERE is_deleted=0";
    if (category) {
        count_sql += " AND category=?";
    }
    Statement cnt = c.prepare(count_sql);
    if (category) {
        cnt.bind(1, *category);
    }
    cnt.step();
    result.total = cnt.col_int(0);
    result.pages = models::calc_pages(result.total, size);

    std::string sql =
        "SELECT id, title, author, category, cover_url, description, "
        "create_time, update_time, is_deleted FROM book_info WHERE is_deleted=0";
    if (category) {
        sql += " AND category=?";
    }
    sql += " ORDER BY create_time DESC LIMIT ? OFFSET ?";

    Statement s = c.prepare(sql);
    int idx = 1;
    if (category) {
        s.bind(idx++, *category);
    }
    s.bind(idx++, size);
    s.bind(idx++, (page - 1) * size);

    while (s.step()) {
        result.items.push_back(row_to_book(s));
    }
    return result;
}

BookInstance instance_create(int64_t book_id, int64_t user_id,
                             int64_t slot_id, int status) {
    Connection& c = conn();
    int64_t iid = generate_id();
    std::string ts = now_str();

    Statement s = c.prepare(
        "INSERT INTO book_instance (instance_id, book_id, user_id, slot_id, status, "
        "create_time, update_time) VALUES (?,?,?,?,?,?,?)");
    s.bind(1, iid)
     .bind(2, book_id)
     .bind(3, user_id)
     .bind(4, slot_id)
     .bind(5, status)
     .bind(6, ts)
     .bind(7, ts);
    s.step();

    LOG_INFO("[DB] 新建实例 instance_id=", iid, " book_id=", book_id, " status=", status);

    // 复用 instance_get 把刚插入的记录读回来（这样 JOIN 字段也是全的）
    auto created = instance_get(iid);
    if (!created) {
        throw std::runtime_error("实例创建后读回失败，数据可能不完整");
    }
    return *created;
}

std::optional<BookInstance> instance_get(int64_t instance_id) {
    // 用统一的 INSTANCE_SELECT（带 JOIN），再加 where 条件
    std::string sql = std::string(INSTANCE_SELECT) +
                      "WHERE i.instance_id=? AND i.is_deleted=0";
    Statement s = conn().prepare(sql);
    s.bind(1, instance_id);
    if (!s.step()) {
        return std::nullopt;
    }
    return row_to_instance(s);
}

bool instance_update(int64_t instance_id, const InstanceUpdate& upd) {
    std::string sql = "UPDATE book_instance SET update_time=?";
    std::vector<std::string> text_params;   // 字符串参数（按出现顺序）
    std::vector<int64_t>     int_params;    // 整数参数

    std::string ts = now_str();

    if (upd.status)      sql += ", status=?";
    if (upd.slot_id)     sql += ", slot_id=?";
    if (upd.borrower_id) sql += ", borrower_id=?";
    if (upd.borrow_time) sql += ", borrow_time=?";
    if (upd.return_time) sql += ", return_time=?";
    if (upd.due_date)    sql += ", due_date=?";

    if (!upd.status && !upd.slot_id && !upd.borrower_id &&
        !upd.borrow_time && !upd.return_time && !upd.due_date) {
        return instance_get(instance_id).has_value();
    }

    sql += " WHERE instance_id=? AND is_deleted=0";

    Statement s = conn().prepare(sql);
    int idx = 1;
    s.bind(idx++, ts);
    // 注意：这里的绑定顺序必须和上面拼 SQL 的顺序完全一致
    if (upd.status)      s.bind(idx++, *upd.status);
    if (upd.slot_id)     s.bind(idx++, *upd.slot_id);
    if (upd.borrower_id) s.bind(idx++, *upd.borrower_id);
    if (upd.borrow_time) s.bind(idx++, *upd.borrow_time);
    if (upd.return_time) s.bind(idx++, *upd.return_time);
    if (upd.due_date)    s.bind(idx++, *upd.due_date);
    s.bind(idx++, instance_id);

    return s.exec_and_changes() > 0;
}

bool instance_delete(int64_t instance_id) {
    Statement s = conn().prepare(
        "UPDATE book_instance SET is_deleted=1, update_time=? "
        "WHERE instance_id=? AND is_deleted=0");
    s.bind(1, now_str()).bind(2, instance_id);
    return s.exec_and_changes() > 0;
}

namespace {

struct StatusFilter {
    std::string sql;             // 形如 " AND ins.status=?" 的片段
    std::vector<int> int_params; // 对应的整数参数
    bool need_now = false;       // 是否还需要绑定一个"当前时间"字符串参数
};

StatusFilter parse_status_filter(const std::string& filter, const std::string& now) {
    (void)now;
    StatusFilter f;

    if (filter.empty()) {
        return f;   // 不过滤
    }

    if (filter == "overdue") {
        // 逾期 = 已借出 + 有应还日期 + 应还日期早于现在
        f.sql = " AND ins.status=? AND ins.due_date!='' AND ins.due_date<?";
        f.int_params.push_back(cfg::ST_BORROWED);
        f.need_now = true;
        return f;
    }

    // 逗号分隔的多状态
    if (filter.find(',') != std::string::npos) {
        std::vector<int> ids;
        std::string cur;
        for (char ch : filter) {
            if (ch == ',') {
                if (!cur.empty()) {
                    ids.push_back(std::stoi(cur));
                    cur.clear();
                }
            } else if (ch != ' ') {
                cur += ch;
            }
        }
        if (!cur.empty()) {
            ids.push_back(std::stoi(cur));
        }

        if (ids.empty()) {
            return f;
        }

        // 拼出 " AND ins.status IN (?,?,?)" —— 有几个状态就几个问号
        f.sql = " AND ins.status IN (";
        for (size_t i = 0; i < ids.size(); ++i) {
            if (i > 0) f.sql += ",";
            f.sql += "?";
        }
        f.sql += ")";
        f.int_params = ids;
        return f;
    }

    // 单个状态
    f.sql = " AND ins.status=?";
    f.int_params.push_back(std::stoi(filter));
    return f;
}

}  // namespace

PageResult<BookInstance> instance_page(int page, int size,
                                       const std::string& status_filter) {
    if (page < 1) page = 1;
    if (size < 1) size = 20;

    PageResult<BookInstance> result;
    result.page = page;
    result.size = size;

    Connection& c = conn();
    std::string now = now_str();
    StatusFilter f = parse_status_filter(status_filter, now);

    // ---- 第一步：数总数 ----
    std::string count_sql =
        "SELECT COUNT(*) FROM book_instance ins WHERE ins.is_deleted=0" + f.sql;
    Statement cnt = c.prepare(count_sql);
    {
        int idx = 1;
        for (int v : f.int_params) {
            cnt.bind(idx++, v);
        }
        if (f.need_now) {
            cnt.bind(idx++, now);
        }
    }
    cnt.step();
    result.total = cnt.col_int(0);
    result.pages = models::calc_pages(result.total, size);

    // ---- 第二步：取当前页 ----
    std::string sql = std::string(INSTANCE_SELECT) +
                      "WHERE i.is_deleted=0" + f.sql +
                      " ORDER BY i.create_time DESC LIMIT ? OFFSET ?";
    Statement s = c.prepare(sql);
    {
        int idx = 1;
        for (int v : f.int_params) {
            s.bind(idx++, v);
        }
        if (f.need_now) {
            s.bind(idx++, now);
        }
        s.bind(idx++, size);
        s.bind(idx++, (page - 1) * size);
    }

    while (s.step()) {
        result.items.push_back(row_to_instance(s));
    }
    return result;
}

std::vector<StorageSlot> slot_list() {
    Connection& c = conn();

    std::string sql =
        "SELECT s.slot_id, s.coord_x, s.coord_y, s.is_occupied, s.remark, "
        "       s.create_time, s.update_time, s.is_deleted, "
        "       COALESCE((SELECT i.instance_id FROM book_instance i "
        "                 WHERE i.slot_id=s.slot_id AND i.is_deleted=0 "
        "                 ORDER BY i.instance_id DESC LIMIT 1), 0) AS instance_id, "
        "       COALESCE((SELECT b.title FROM book_instance i "
        "                 JOIN book_info b ON b.id=i.book_id "
        "                 WHERE i.slot_id=s.slot_id AND i.is_deleted=0 "
        "                 ORDER BY i.instance_id DESC LIMIT 1), '') AS title "
        "FROM storage_slot s "
        "WHERE s.is_deleted=0 ORDER BY s.slot_id";

    Statement st = c.prepare(sql);
    std::vector<StorageSlot> out;
    while (st.step()) {
        StorageSlot sl = row_to_slot(st);
        // row_to_slot 只读了前 8 列，后两列（instance_id / title）单独取
        sl.instance_id = st.col_int64(8);
        sl.title       = st.col_text(9);
        out.push_back(sl);
    }
    return out;
}

std::optional<StorageSlot> slot_get(int64_t slot_id) {
    Statement s = conn().prepare(
        "SELECT slot_id, coord_x, coord_y, is_occupied, remark, "
        "create_time, update_time, is_deleted "
        "FROM storage_slot WHERE slot_id=? AND is_deleted=0");
    s.bind(1, slot_id);
    if (!s.step()) {
        return std::nullopt;
    }
    return row_to_slot(s);
}

bool slot_update(int64_t slot_id, const SlotUpdate& upd) {
    std::string sql = "UPDATE storage_slot SET update_time=?";
    std::string ts = now_str();

    if (upd.coord_x)     sql += ", coord_x=?";
    if (upd.coord_y)     sql += ", coord_y=?";
    if (upd.is_occupied) sql += ", is_occupied=?";
    if (upd.remark)      sql += ", remark=?";

    if (!upd.coord_x && !upd.coord_y && !upd.is_occupied && !upd.remark) {
        return slot_get(slot_id).has_value();
    }

    sql += " WHERE slot_id=? AND is_deleted=0";

    Statement s = conn().prepare(sql);
    int idx = 1;
    s.bind(idx++, ts);
    if (upd.coord_x)     s.bind(idx++, *upd.coord_x);
    if (upd.coord_y)     s.bind(idx++, *upd.coord_y);
    if (upd.is_occupied) s.bind(idx++, *upd.is_occupied);
    if (upd.remark)      s.bind(idx++, *upd.remark);
    s.bind(idx++, slot_id);

    return s.exec_and_changes() > 0;
}

AllocResult scan_allocate(int64_t book_id, const std::string& category, int status) {
    Connection& c = conn();
    std::string ts = now_str();

    // ---- 1. 找同分类区域的空格子 ----
    // 分类 → 备注前缀，例如 "文学" → "文学区"
    const char* prefix = "";
    for (const auto& kv : CATEGORY_PREFIXES) {
        if (category == kv.first) {
            prefix = kv.second;
            break;
        }
    }

    std::optional<StorageSlot> slot;

    if (prefix[0] != '\0') {
        // LIKE '文学区%' —— % 是通配符，匹配任意后缀
        Statement s = c.prepare(
            "SELECT slot_id, coord_x, coord_y, is_occupied, remark, "
            "create_time, update_time, is_deleted "
            "FROM storage_slot WHERE is_occupied=0 AND is_deleted=0 "
            "AND remark LIKE ? ORDER BY slot_id LIMIT 1");
        s.bind(1, std::string(prefix) + "%");
        if (s.step()) {
            slot = row_to_slot(s);
        }
    }

    // ---- 2. 同分类满了，溢出到任意空格子 ----
    if (!slot) {
        Statement s = c.prepare(
            "SELECT slot_id, coord_x, coord_y, is_occupied, remark, "
            "create_time, update_time, is_deleted "
            "FROM storage_slot WHERE is_occupied=0 AND is_deleted=0 "
            "ORDER BY slot_id LIMIT 1");
        if (s.step()) {
            slot = row_to_slot(s);
        }
    }

    if (!slot) {
        throw std::runtime_error("无空闲槽位（书架已满）");
    }

    c.exec("BEGIN IMMEDIATE");

    try {
        // ---- 4. 乐观锁抢格子 ----
        Statement claim = c.prepare(
            "UPDATE storage_slot SET is_occupied=1, update_time=? "
            "WHERE slot_id=? AND is_occupied=0");
        claim.bind(1, ts).bind(2, slot->slot_id);

        if (claim.exec_and_changes() == 0) {
            // 影响行数为 0 = 这个格子刚被别人抢走了
            throw std::runtime_error("槽位已被占用，请重试");
        }

        // ---- 5. 找到要上架的那本书本实例 ----
        // 按 book_id 找最新创建的那一条（同一本书可能有多本实体）
        Statement find = c.prepare(
            "SELECT instance_id FROM book_instance "
            "WHERE book_id=? AND is_deleted=0 ORDER BY create_time DESC LIMIT 1");
        find.bind(1, book_id);

        if (!find.step()) {
            // 找不到实例 → 抛异常 → 事务回滚 → 格子自动恢复
            throw std::runtime_error("未找到书本实例（book_id=" +
                                     std::to_string(book_id) + "）");
        }
        int64_t instance_id = find.col_int64(0);

        // ---- 6. 更新实例：绑定格子 + 改状态 ----
        Statement upd = c.prepare(
            "UPDATE book_instance SET slot_id=?, status=?, update_time=? "
            "WHERE instance_id=?");
        upd.bind(1, slot->slot_id)
           .bind(2, status)
           .bind(3, ts)
           .bind(4, instance_id);
        upd.step();

        // ---- 7. 提交 ----
        c.exec("COMMIT");

        AllocResult r;
        r.instance_id = instance_id;
        r.slot_id     = slot->slot_id;
        r.coord_x     = slot->coord_x;
        r.coord_y     = slot->coord_y;
        r.remark      = slot->remark;

        LOG_INFO("[DB] 分配槽位：书 ", book_id, " → 格子 ", slot->slot_id,
                 " (", slot->coord_x, "层 ", slot->coord_y, "号 ",
                 slot->remark, ")");

        return r;

    } catch (...) {
        c.exec("ROLLBACK");
        LOG_WARN("[DB] 分配槽位失败，已回滚（book_id=", book_id, "）");
        throw;
    }
}

bool slot_release(int64_t slot_id) {
    Statement s = conn().prepare(
        "UPDATE storage_slot SET is_occupied=0, update_time=? "
        "WHERE slot_id=? AND is_deleted=0");
    s.bind(1, now_str()).bind(2, slot_id);
    return s.exec_and_changes() > 0;
}

std::vector<BookInfo> book_search(const std::string& keyword) {
    Statement s = conn().prepare(
        "SELECT id, title, author, category, cover_url, description, "
        "create_time, update_time, is_deleted "
        "FROM book_info WHERE is_deleted=0 AND title LIKE ? "
        "ORDER BY create_time DESC LIMIT 50");
    s.bind(1, "%" + keyword + "%");

    std::vector<BookInfo> out;
    while (s.step()) {
        out.push_back(row_to_book(s));
    }
    return out;
}

std::vector<BookInstance> instance_shelf_list() {
    std::string sql = std::string(INSTANCE_SELECT) +
                      "WHERE i.is_deleted=0 AND i.status=? ORDER BY i.slot_id";
    Statement s = conn().prepare(sql);
    s.bind(1, cfg::ST_ON_SHELF);

    std::vector<BookInstance> out;
    while (s.step()) {
        out.push_back(row_to_instance(s));
    }
    return out;
}

std::vector<BookInstance> instance_borrowed_list(int64_t user_id) {
    std::string sql = std::string(INSTANCE_SELECT) + "WHERE i.is_deleted=0 AND i.status=?";
    if (user_id > 0) {
        sql += " AND i.borrower_id=?";
    }
    sql += " ORDER BY i.borrow_time DESC";

    Statement s = conn().prepare(sql);
    int idx = 1;
    s.bind(idx++, cfg::ST_BORROWED);
    if (user_id > 0) {
        s.bind(idx++, user_id);
    }

    std::vector<BookInstance> out;
    while (s.step()) {
        out.push_back(row_to_instance(s));
    }
    return out;
}

std::optional<BookInstance> instance_get_by_slot(int64_t slot_id) {
    std::string sql = std::string(INSTANCE_SELECT) +
                      "WHERE i.slot_id=? AND i.is_deleted=0 AND i.status=? LIMIT 1";
    Statement s = conn().prepare(sql);
    s.bind(1, slot_id).bind(2, cfg::ST_ON_SHELF);
    if (!s.step()) {
        return std::nullopt;
    }
    return row_to_instance(s);
}

std::optional<BookInstance> instance_get_on_shelf_by_book_id(int64_t book_id) {
    // 扫码时用：二维码里存的是 book_id，要找到"在架"的那一本实体书
    std::string sql = std::string(INSTANCE_SELECT) +
                      "WHERE i.book_id=? AND i.is_deleted=0 AND i.status=? "
                      "ORDER BY i.create_time DESC LIMIT 1";
    Statement s = conn().prepare(sql);
    s.bind(1, book_id).bind(2, cfg::ST_ON_SHELF);
    if (!s.step()) {
        return std::nullopt;
    }
    return row_to_instance(s);
}

std::optional<BookInstance> instance_get_by_book_id_any(int64_t book_id) {
    std::string sql = std::string(INSTANCE_SELECT) +
                      "WHERE i.book_id=? AND i.is_deleted=0 "
                      "ORDER BY i.create_time DESC LIMIT 1";
    Statement s = conn().prepare(sql);
    s.bind(1, book_id);
    if (!s.step()) {
        return std::nullopt;
    }
    return row_to_instance(s);
}

std::optional<BookInstance> instance_get_with_slot(int64_t instance_id) {
    // 和 instance_get 一样的实现（INSTANCE_SELECT 已经 JOIN 了槽位表）
    return instance_get(instance_id);
}

int user_borrow_count(int64_t user_id) {
    Statement s = conn().prepare(
        "SELECT COUNT(*) FROM book_instance "
        "WHERE borrower_id=? AND status=? AND is_deleted=0");
    s.bind(1, user_id).bind(2, cfg::ST_BORROWED);
    s.step();
    return s.col_int(0);
}

bool user_has_overdue(int64_t user_id) {
    Statement s = conn().prepare(
        "SELECT COUNT(*) FROM book_instance "
        "WHERE borrower_id=? AND status=? AND is_deleted=0 "
        "AND due_date!='' AND due_date<?");
    s.bind(1, user_id).bind(2, cfg::ST_BORROWED).bind(3, now_str());
    s.step();
    return s.col_int(0) > 0;
}

bool user_is_frozen(int64_t user_id) {
    auto u = user_get(user_id);
    return u.has_value() && u->is_frozen == 1;
}

void user_add_fine(int64_t user_id, double amount) {
    if (amount <= 0) {
        return;
    }
    // round 到分：金额是钱，不能有 0.001 元这种精度
    // 用 (x * 100 + 0.5) / 100 的方式取整到两位小数
    double rounded = std::round(amount * 100.0) / 100.0;

    Statement s = conn().prepare(
        "UPDATE \"user\" SET fine_balance = fine_balance + ?, update_time=? "
        "WHERE user_id=?");
    s.bind(1, rounded).bind(2, now_str()).bind(3, user_id);
    s.step();
}

double user_get_fine(int64_t user_id) {
    auto u = user_get(user_id);
    if (!u) return 0.0;
    return std::round(u->fine_balance * 100.0) / 100.0;
}

namespace {

bool parse_time(const std::string& text, std::time_t& out) {
    std::tm tm_buf{};
    if (strptime(text.c_str(), "%Y-%m-%d %H:%M:%S", &tm_buf) == nullptr) {
        return false;
    }
    out = mktime(&tm_buf);   // mktime 会按本地时区解释 tm
    return out != static_cast<std::time_t>(-1);
}

// 当前时间往后 n 天的字符串
std::string now_plus_days(int days) {
    std::time_t t = std::time(nullptr) + static_cast<std::time_t>(days) * 86400;
    std::tm tm_buf{};
    localtime_r(&t, &tm_buf);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm_buf);
    return std::string(buf);
}

}  // namespace

BookInstance borrow_instance(int64_t instance_id, int64_t user_id) {
    Connection& c = conn();

    // ---- 1. 查书 ----
    auto found = instance_get(instance_id);
    if (!found) {
        throw std::runtime_error("书本实例不存在");
    }
    BookInstance inst = *found;

    // ---- 2-3. 状态校验 ----
    if (inst.status == cfg::ST_BORROWED) {
        throw std::runtime_error("该书已被借出");
    }
    if (inst.status == cfg::ST_PENDING_STICKER) {
        throw std::runtime_error("该书尚未上架，无法借阅");
    }

    // ---- 4-6. 用户校验 ----
    if (user_is_frozen(user_id)) {
        throw std::runtime_error("账号已冻结，无法借阅");
    }
    if (user_borrow_count(user_id) >= cfg::MAX_BORROW_COUNT) {
        throw std::runtime_error("已达借书上限（" +
                                 std::to_string(cfg::MAX_BORROW_COUNT) +
                                 "本），请先归还");
    }
    if (user_has_overdue(user_id)) {
        throw std::runtime_error("有逾期未还的书，请先归还后再借");
    }

    // ---- 7. 算应还日期 ----
    std::string ts = now_str();
    std::string due_date = now_plus_days(cfg::BORROW_DAYS);

    // ---- 8-9. 用事务保证"释放格子"和"改状态"要么都成功要么都不做 ----
    c.exec("BEGIN IMMEDIATE");
    try {
        if (inst.slot_id != 0) {
            Statement rel = c.prepare(
                "UPDATE storage_slot SET is_occupied=0, update_time=? WHERE slot_id=?");
            rel.bind(1, ts).bind(2, inst.slot_id);
            rel.step();
        }

        Statement upd = c.prepare(
            "UPDATE book_instance SET status=?, slot_id=0, borrower_id=?, "
            "borrow_time=?, due_date=?, return_time='', update_time=? "
            "WHERE instance_id=?");
        upd.bind(1, cfg::ST_BORROWED)
           .bind(2, user_id)
           .bind(3, ts)
           .bind(4, due_date)
           .bind(5, ts)
           .bind(6, instance_id);
        upd.step();

        c.exec("COMMIT");
    } catch (...) {
        c.exec("ROLLBACK");
        throw;
    }

    LOG_INFO("[DB] 借书成功 instance=", instance_id,
             " user=", user_id, " 应还=", due_date);

    auto updated = instance_get(instance_id);
    if (!updated) {
        throw std::runtime_error("借书后读回记录失败");
    }
    return *updated;
}

ReturnResult return_instance(int64_t instance_id) {
    Connection& c = conn();

    auto found = instance_get(instance_id);
    if (!found) {
        throw std::runtime_error("书本实例不存在");
    }
    BookInstance inst = *found;

    if (inst.status != cfg::ST_BORROWED) {
        throw std::runtime_error("该书未被借出，无需归还");
    }

    // ---- 算逾期 ----
    int overdue_days = 0;
    double overdue_fine = 0.0;

    if (!inst.due_date.empty()) {
        std::time_t due = 0;
        if (parse_time(inst.due_date, due)) {
            std::time_t now = std::time(nullptr);
            // difftime 返回秒数差（double）
            double diff_days = std::difftime(now, due) / 86400.0;
            if (diff_days > 0) {
                overdue_days = static_cast<int>(diff_days);
            }
            overdue_fine = std::round(overdue_days * cfg::FINE_PER_DAY * 100.0) / 100.0;
        }
    }

    std::string ts = now_str();

    Statement upd = c.prepare(
        "UPDATE book_instance SET status=?, borrower_id=0, return_time=?, "
        "update_time=? WHERE instance_id=?");
    upd.bind(1, cfg::ST_PENDING_SHELF)
       .bind(2, ts)
       .bind(3, ts)
       .bind(4, instance_id);
    upd.step();

    LOG_INFO("[DB] 还书 instance=", instance_id,
             " 逾期=", overdue_days, "天 罚款=", overdue_fine, "元");

    ReturnResult r;
    auto updated = instance_get(instance_id);
    if (!updated) {
        throw std::runtime_error("还书后读回记录失败");
    }
    r.instance = *updated;
    r.overdue_days = overdue_days;
    r.fine = overdue_fine;
    return r;
}

AllocResult re_shelve_instance(int64_t instance_id, const std::string& category) {
    Connection& c = conn();

    auto found = instance_get(instance_id);
    if (!found) {
        throw std::runtime_error("书本实例不存在");
    }
    if (found->status != cfg::ST_PENDING_SHELF) {
        throw std::runtime_error("该书不是待上架状态");
    }

    // ---- 找同分类空格子，找不到就溢出 ----
    const char* prefix = "";
    for (const auto& kv : CATEGORY_PREFIXES) {
        if (category == kv.first) {
            prefix = kv.second;
            break;
        }
    }

    std::optional<StorageSlot> slot;
    if (prefix[0] != '\0') {
        Statement s = c.prepare(
            "SELECT slot_id, coord_x, coord_y, is_occupied, remark, "
            "create_time, update_time, is_deleted "
            "FROM storage_slot WHERE is_occupied=0 AND is_deleted=0 "
            "AND remark LIKE ? ORDER BY slot_id LIMIT 1");
        s.bind(1, std::string(prefix) + "%");
        if (s.step()) slot = row_to_slot(s);
    }
    if (!slot) {
        Statement s = c.prepare(
            "SELECT slot_id, coord_x, coord_y, is_occupied, remark, "
            "create_time, update_time, is_deleted "
            "FROM storage_slot WHERE is_occupied=0 AND is_deleted=0 "
            "ORDER BY slot_id LIMIT 1");
        if (s.step()) slot = row_to_slot(s);
    }
    if (!slot) {
        throw std::runtime_error("无空闲槽位（书架已满）");
    }

    std::string ts = now_str();
    c.exec("BEGIN IMMEDIATE");
    try {
        Statement claim = c.prepare(
            "UPDATE storage_slot SET is_occupied=1, update_time=? "
            "WHERE slot_id=? AND is_occupied=0");
        claim.bind(1, ts).bind(2, slot->slot_id);
        if (claim.exec_and_changes() == 0) {
            throw std::runtime_error("槽位已被占用，请重试");
        }

        Statement upd = c.prepare(
            "UPDATE book_instance SET slot_id=?, status=?, update_time=? "
            "WHERE instance_id=?");
        upd.bind(1, slot->slot_id)
           .bind(2, cfg::ST_ON_SHELF)
           .bind(3, ts)
           .bind(4, instance_id);
        upd.step();

        c.exec("COMMIT");
    } catch (...) {
        c.exec("ROLLBACK");
        throw;
    }

    AllocResult r;
    r.instance_id = instance_id;
    r.slot_id     = slot->slot_id;
    r.coord_x     = slot->coord_x;
    r.coord_y     = slot->coord_y;
    r.remark      = slot->remark;

    LOG_INFO("[DB] 重新上架 instance=", instance_id,
             " → 格子 ", slot->slot_id, " (", slot->remark, ")");
    return r;
}

User user_create(const std::string& openid, const std::string& nickname,
                 const std::string& avatar_url, int role) {
    Connection& c = conn();
    std::string ts = now_str();

    Statement s = c.prepare(
        "INSERT INTO \"user\" (openid, nickname, avatar_url, role, is_frozen, "
        "fine_balance, create_time, update_time, is_deleted) "
        "VALUES (?,?,?,?,0,0,?,?,0)");
    s.bind(1, openid)
     .bind(2, nickname)
     .bind(3, avatar_url)
     .bind(4, role)
     .bind(5, ts)
     .bind(6, ts);
    s.step();

    int64_t uid = c.last_insert_rowid();
    LOG_INFO("[DB] 新建用户 user_id=", uid, " 昵称=", nickname);

    auto u = user_get(uid);
    if (!u) {
        throw std::runtime_error("用户创建后读回失败");
    }
    return *u;
}

namespace {

// user 表统一的 SELECT（避免每处都写一遍列名）
const char* USER_SELECT =
    "SELECT user_id, openid, nickname, avatar_url, role, is_frozen, fine_balance, "
    "create_time, update_time, is_deleted FROM \"user\" ";

}  // namespace

std::optional<User> user_get(int64_t user_id) {
    Statement s = conn().prepare(std::string(USER_SELECT) +
                                 "WHERE user_id=? AND is_deleted=0");
    s.bind(1, user_id);
    if (!s.step()) {
        return std::nullopt;
    }
    User u = row_to_user(s);

    // 顺便统计"当前借了几本书"（前端用户列表要显示）
    u.borrow_count = user_borrow_count(user_id);
    return u;
}

std::optional<User> user_get_by_openid(const std::string& openid) {
    Statement s = conn().prepare(std::string(USER_SELECT) +
                                 "WHERE openid=? AND is_deleted=0 LIMIT 1");
    s.bind(1, openid);
    if (!s.step()) {
        return std::nullopt;
    }
    User u = row_to_user(s);
    u.borrow_count = user_borrow_count(u.user_id);
    return u;
}

PageResult<User> user_list(int page, int size) {
    if (page < 1) page = 1;
    if (size < 1) size = 20;

    PageResult<User> result;
    result.page = page;
    result.size = size;

    Connection& c = conn();

    Statement cnt = c.prepare("SELECT COUNT(*) FROM \"user\" WHERE is_deleted=0");
    cnt.step();
    result.total = cnt.col_int(0);
    result.pages = models::calc_pages(result.total, size);

    Statement s = c.prepare(std::string(USER_SELECT) +
                            "WHERE is_deleted=0 ORDER BY create_time DESC "
                            "LIMIT ? OFFSET ?");
    s.bind(1, size).bind(2, (page - 1) * size);

    while (s.step()) {
        User u = row_to_user(s);
        u.borrow_count = user_borrow_count(u.user_id);
        result.items.push_back(u);
    }
    return result;
}

bool user_update(int64_t user_id, const UserUpdate& upd) {
    std::string sql = "UPDATE \"user\" SET update_time=?";
    std::string ts = now_str();

    if (upd.nickname)   sql += ", nickname=?";
    if (upd.avatar_url) sql += ", avatar_url=?";
    if (upd.role)       sql += ", role=?";
    if (upd.is_frozen)  sql += ", is_frozen=?";

    if (!upd.nickname && !upd.avatar_url && !upd.role && !upd.is_frozen) {
        return user_get(user_id).has_value();
    }

    sql += " WHERE user_id=? AND is_deleted=0";

    Statement s = conn().prepare(sql);
    int idx = 1;
    s.bind(idx++, ts);
    if (upd.nickname)   s.bind(idx++, *upd.nickname);
    if (upd.avatar_url) s.bind(idx++, *upd.avatar_url);
    if (upd.role)       s.bind(idx++, *upd.role);
    if (upd.is_frozen)  s.bind(idx++, *upd.is_frozen);
    s.bind(idx++, user_id);

    return s.exec_and_changes() > 0;
}

OperationLog log_create(int64_t instance_id, int64_t user_id,
                        const std::string& action_type, const std::string& content,
                        int status, const std::string& operator_name) {
    Connection& c = conn();
    std::string ts = now_str();

    Statement s = c.prepare(
        "INSERT INTO operation_log (instance_id, user_id, action_type, content, "
        "status, create_time, operator_name) VALUES (?,?,?,?,?,?,?)");
    s.bind(1, instance_id)
     .bind(2, user_id)
     .bind(3, action_type)
     .bind(4, content)
     .bind(5, status)
     .bind(6, ts)
     .bind(7, operator_name);
    s.step();

    OperationLog lg;
    lg.log_id        = c.last_insert_rowid();
    lg.instance_id   = instance_id;
    lg.user_id       = user_id;
    lg.action_type   = action_type;
    lg.content       = content;
    lg.status        = status;
    lg.create_time   = ts;
    lg.operator_name = operator_name;
    return lg;
}

std::optional<OperationLog> log_get(int64_t log_id) {
    Statement s = conn().prepare(
        "SELECT log_id, instance_id, user_id, action_type, content, status, "
        "create_time, operator_name FROM operation_log WHERE log_id=?");
    s.bind(1, log_id);
    if (!s.step()) {
        return std::nullopt;
    }
    return row_to_log(s);
}

PageResult<OperationLog> log_page(int page, int size) {
    if (page < 1) page = 1;
    if (size < 1) size = 20;

    PageResult<OperationLog> result;
    result.page = page;
    result.size = size;

    Connection& c = conn();

    Statement cnt = c.prepare("SELECT COUNT(*) FROM operation_log");
    cnt.step();
    result.total = cnt.col_int(0);
    result.pages = models::calc_pages(result.total, size);

    Statement s = c.prepare(
        "SELECT log_id, instance_id, user_id, action_type, content, status, "
        "create_time, operator_name FROM operation_log "
        "ORDER BY log_id DESC LIMIT ? OFFSET ?");
    s.bind(1, size).bind(2, (page - 1) * size);

    while (s.step()) {
        result.items.push_back(row_to_log(s));
    }
    return result;
}

std::vector<OperationLog> log_by_instance(int64_t instance_id) {
    Statement s = conn().prepare(
        "SELECT log_id, instance_id, user_id, action_type, content, status, "
        "create_time, operator_name FROM operation_log "
        "WHERE instance_id=? ORDER BY log_id DESC");
    s.bind(1, instance_id);

    std::vector<OperationLog> out;
    while (s.step()) {
        out.push_back(row_to_log(s));
    }
    return out;
}

void task_save(const TaskRow& row) {
    Statement s = conn().prepare(
        "INSERT OR REPLACE INTO task "
        "(task_id, action, user_id, payload, status, result, created_at, expires_at, completed_at) "
        "VALUES (?,?,?,?,?,?,?,?,?)");
    s.bind(1, row.task_id)
     .bind(2, row.action)
     .bind(3, row.user_id)
     .bind(4, row.payload)
     .bind(5, row.status)
     .bind(6, row.result)
     .bind(7, row.created_at)
     .bind(8, row.expires_at)
     .bind(9, row.completed_at);
    s.step();
}

std::optional<TaskRow> task_load(const std::string& task_id) {
    Statement s = conn().prepare(
        "SELECT task_id, action, user_id, payload, status, result, "
        "created_at, expires_at, completed_at FROM task WHERE task_id=?");
    s.bind(1, task_id);
    if (!s.step()) {
        return std::nullopt;
    }
    TaskRow r;
    r.task_id      = s.col_text(0);
    r.action       = s.col_text(1);
    r.user_id      = s.col_int64(2);
    r.payload      = s.col_text(3);
    r.status       = s.col_text(4);
    r.result       = s.col_text(5);
    r.created_at   = s.col_text(6);
    r.expires_at   = s.col_text(7);
    r.completed_at = s.col_text(8);
    return r;
}

void task_set_status(const std::string& task_id, const std::string& status,
                     const std::string& result, const std::string& completed_at) {
    Statement s = conn().prepare(
        "UPDATE task SET status=?, result=?, completed_at=? WHERE task_id=?");
    s.bind(1, status).bind(2, result).bind(3, completed_at).bind(4, task_id);
    s.step();
}

std::vector<TaskRow> task_list_recent(int limit) {
    if (limit < 1) limit = 50;
    Statement s = conn().prepare(
        "SELECT task_id, action, user_id, payload, status, result, "
        "created_at, expires_at, completed_at FROM task "
        "ORDER BY created_at DESC LIMIT ?");
    s.bind(1, limit);

    std::vector<TaskRow> out;
    while (s.step()) {
        TaskRow r;
        r.task_id      = s.col_text(0);
        r.action       = s.col_text(1);
        r.user_id      = s.col_int64(2);
        r.payload      = s.col_text(3);
        r.status       = s.col_text(4);
        r.result       = s.col_text(5);
        r.created_at   = s.col_text(6);
        r.expires_at   = s.col_text(7);
        r.completed_at = s.col_text(8);
        out.push_back(r);
    }
    return out;
}

}  // namespace db
