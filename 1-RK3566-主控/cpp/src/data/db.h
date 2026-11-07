// db.h
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

std::string current_path();

int64_t generate_id();

std::string now_str();

BookInfo book_create(const std::string& title,
                     const std::string& category = "",
                     const std::string& cover_url = "",
                     const std::string& description = "",
                     const std::string& author = "");

std::optional<BookInfo> book_get(int64_t book_id);

struct BookUpdate {
    std::optional<std::string> title;
    std::optional<std::string> category;
    std::optional<std::string> cover_url;
    std::optional<std::string> description;
    std::optional<std::string> author;
};
bool book_update(int64_t book_id, const BookUpdate& upd);

bool book_delete(int64_t book_id);

PageResult<BookInfo> book_list(int page = 1, int size = 20,
                               const std::optional<std::string>& category = std::nullopt);

BookInstance instance_create(int64_t book_id, int64_t user_id = 0,
                             int64_t slot_id = 0, int status = 0);

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

bool slot_release(int64_t slot_id);

std::vector<BookInfo> book_search(const std::string& keyword);

std::vector<BookInstance> instance_shelf_list();

std::vector<BookInstance> instance_borrowed_list(int64_t user_id = 0);

std::optional<BookInstance> instance_get_by_slot(int64_t slot_id);

std::optional<BookInstance> instance_get_on_shelf_by_book_id(int64_t book_id);

std::optional<BookInstance> instance_get_by_book_id_any(int64_t book_id);

std::optional<BookInstance> instance_get_with_slot(int64_t instance_id);

int  user_borrow_count(int64_t user_id);
bool user_has_overdue(int64_t user_id);
bool user_is_frozen(int64_t user_id);
void user_add_fine(int64_t user_id, double amount);
double user_get_fine(int64_t user_id);

BookInstance borrow_instance(int64_t instance_id, int64_t user_id);

struct ReturnResult {
    BookInstance instance;
    int overdue_days = 0;
    double fine = 0.0;
};
ReturnResult return_instance(int64_t instance_id);

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
    std::string payload;
    std::string status;
    std::string result;
    std::string created_at;
    std::string expires_at;
    std::string completed_at;
};

void task_save(const TaskRow& row);
std::optional<TaskRow> task_load(const std::string& task_id);
void task_set_status(const std::string& task_id, const std::string& status,
                     const std::string& result, const std::string& completed_at);
std::vector<TaskRow> task_list_recent(int limit = 50);

}
