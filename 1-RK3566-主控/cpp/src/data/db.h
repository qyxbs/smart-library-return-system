// =============================================================================
// =============================================================================
// 【这一层负责什么】
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "data/models.h"

namespace db {

using models::BookInfo;
using models::BookInstance;
using models::OperationLog;
using models::PageResult;
using models::StorageSlot;
using models::User;

void init(const std::string& path = "");

// 返回当前使用的数据库文件路径（日志里打出来方便确认）
std::string current_path();

int64_t generate_id();

// 当前时间的标准格式字符串："2026-11-04 16:23:45"
// 全库所有时间字段都用这个格式，好处是同格式字符串可以直接比大小
std::string now_str();

// 创建一条书目记录，返回完整记录（含新生成的 id）
BookInfo book_create(const std::string& title,
                     const std::string& category = "",
                     const std::string& cover_url = "",
                     const std::string& description = "",
                     const std::string& author = "");

// 按 id 查书目；不存在返回 std::nullopt
std::optional<BookInfo> book_get(int64_t book_id);

// 更新书目。只更新 Update 里"有值"的字段。
// 返回 true = 确实改了一行；false = 记录不存在。
struct BookUpdate {
    std::optional<std::string> title;
    std::optional<std::string> category;
    std::optional<std::string> cover_url;
    std::optional<std::string> description;
    std::optional<std::string> author;
};
bool book_update(int64_t book_id, const BookUpdate& upd);

// 软删除（把 is_deleted 置 1，数据还留着）。返回是否命中一行。
bool book_delete(int64_t book_id);

// 分页列出书目。category 为空表示不按分类过滤。
PageResult<BookInfo> book_list(int page = 1, int size = 20,
                               const std::optional<std::string>& category = std::nullopt);

BookInstance instance_create(int64_t book_id, int64_t user_id = 0,
                             int64_t slot_id = 0, int status = 0);

// 查实例（含 JOIN 出来的书名/坐标/借阅人）
std::optional<BookInstance> instance_get(int64_t instance_id);

struct InstanceUpdate {
    std::optional<int> status;
    std::optional<int64_t> slot_id;
    std::optional<int64_t> borrower_id;
    std::optional<std::string> borrow_time;
    std::optional<std::string> return_time;
    std::optional<std::string> due_date;
};
bool instance_update(int64_t instance_id, const InstanceUpdate& upd);

bool instance_delete(int64_t instance_id);

PageResult<BookInstance> instance_page(int page = 1, int size = 20,
                                       const std::string& status_filter = "");

// 列出全部 16 个格子（含每格放的书的书名，方便前端直接显示）
std::vector<StorageSlot> slot_list();

std::optional<StorageSlot> slot_get(int64_t slot_id);

struct SlotUpdate {
    std::optional<int> coord_x;
    std::optional<int> coord_y;
    std::optional<int> is_occupied;
    std::optional<std::string> remark;
};
bool slot_update(int64_t slot_id, const SlotUpdate& upd);

struct AllocResult {
    int64_t instance_id = 0;
    int64_t slot_id = 0;
    int coord_x = 0;
    int coord_y = 0;
    std::string remark;
};
AllocResult scan_allocate(int64_t book_id, const std::string& category, int status = 3);

// 释放格子（书被借走/下架时调用）。返回是否命中一行。
bool slot_release(int64_t slot_id);

// 按关键词模糊搜索书名/作者
std::vector<BookInfo> book_search(const std::string& keyword);

// 书架上的书（status=3，含坐标），用于书架看板
std::vector<BookInstance> instance_shelf_list();

// 某人当前借阅中的书（user_id=0 表示所有用户的借阅记录）
std::vector<BookInstance> instance_borrowed_list(int64_t user_id = 0);

// 按格子查书
std::optional<BookInstance> instance_get_by_slot(int64_t slot_id);

// 按书目 ID 查"在架"的那一本（扫码时用：二维码里存的是 book_id）
std::optional<BookInstance> instance_get_on_shelf_by_book_id(int64_t book_id);

// 按书目 ID 查任意状态的一本
std::optional<BookInstance> instance_get_by_book_id_any(int64_t book_id);

// 查实例 + 槽位信息（K230 桥接用）
std::optional<BookInstance> instance_get_with_slot(int64_t instance_id);

int  user_borrow_count(int64_t user_id);       // 当前借了几本
bool user_has_overdue(int64_t user_id);        // 有没有逾期未还的
bool user_is_frozen(int64_t user_id);          // 账号是否被冻结
void user_add_fine(int64_t user_id, double amount);   // 累加罚款
double user_get_fine(int64_t user_id);         // 查欠费金额

// 借书：校验规则 → 记录借阅人/时间/应还日期 → 释放槽位 → 状态改"已借出"
// 返回更新后的实例；校验不通过抛 std::runtime_error（异常信息是给用户看的中文提示）
BookInstance borrow_instance(int64_t instance_id, int64_t user_id);

// 还书：状态改"待上架"，算逾期天数，逾期自动累加罚款
// 返回值里额外带 overdue_days / fine（见 ReturnResult）
struct ReturnResult {
    BookInstance instance;
    int overdue_days = 0;      // 逾期天数（0 = 没逾期）
    double fine = 0.0;         // 本次产生的罚款
};
ReturnResult return_instance(int64_t instance_id);

// 重新上架：给"待上架"的书重新分配格子，状态改"在架"
AllocResult re_shelve_instance(int64_t instance_id, const std::string& category);

User user_create(const std::string& openid = "",
                 const std::string& nickname = "",
                 const std::string& avatar_url = "",
                 int role = 0);

std::optional<User> user_get(int64_t user_id);
std::optional<User> user_get_by_openid(const std::string& openid);

PageResult<User> user_list(int page = 1, int size = 20);

struct UserUpdate {
    std::optional<std::string> nickname;
    std::optional<std::string> avatar_url;
    std::optional<int> role;
    std::optional<int> is_frozen;
};
bool user_update(int64_t user_id, const UserUpdate& upd);

OperationLog log_create(int64_t instance_id = 0, int64_t user_id = 0,
                        const std::string& action_type = "",
                        const std::string& content = "",
                        int status = 1,
                        const std::string& operator_name = "");

std::optional<OperationLog> log_get(int64_t log_id);
PageResult<OperationLog> log_page(int page = 1, int size = 20);
std::vector<OperationLog> log_by_instance(int64_t instance_id);

struct TaskRow {
    std::string task_id;
    std::string action;
    int64_t user_id = 0;
    std::string payload;         // JSON 字符串
    std::string status;
    std::string result;          // JSON 字符串
    std::string created_at;
    std::string expires_at;
    std::string completed_at;
};

void task_save(const TaskRow& row);                        // INSERT OR REPLACE
std::optional<TaskRow> task_load(const std::string& task_id);
void task_set_status(const std::string& task_id, const std::string& status,
                     const std::string& result, const std::string& completed_at);
std::vector<TaskRow> task_list_recent(int limit = 50);

}  // namespace db
