// models.h —— 数据结构定义（对应数据库里的表）
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace models {

struct BookInfo {
    int64_t id = 0;
    std::string title;
    std::string author;
    std::string category;
    std::string cover_url;
    std::string description;
    std::string create_time;
    std::string update_time;
    int is_deleted = 0;
};

struct BookInstance {
    int64_t instance_id = 0;
    int64_t book_id = 0;
    int64_t user_id = 0;
    int64_t slot_id = 0;
    int status = 0;
    int64_t borrower_id = 0;
    std::string borrow_time;
    std::string return_time;
    std::string due_date;
    std::string create_time;
    std::string update_time;
    int is_deleted = 0;

    std::string title;
    std::string category;
    int coord_x = 0;
    int coord_y = 0;
    std::string remark;
    std::string borrower_name;
};

struct StorageSlot {
    int64_t slot_id = 0;
    int coord_x = 0;
    int coord_y = 0;
    int is_occupied = 0;
    std::string remark;
    std::string create_time;
    std::string update_time;
    int is_deleted = 0;

    int64_t instance_id = 0;
    std::string title;
};

struct User {
    int64_t user_id = 0;
    std::string openid;
    std::string nickname;
    std::string avatar_url;
    int role = 0;
    int is_frozen = 0;                 // 是否冻结（逾期未还会被冻结，冻结后不能借书）
    double fine_balance = 0.0;
    std::string create_time;
    std::string update_time;
    int is_deleted = 0;

    int borrow_count = 0;
};

struct OperationLog {
    int64_t log_id = 0;
    int64_t instance_id = 0;
    int64_t user_id = 0;
    std::string action_type;
    std::string content;
    std::string operator_name;
    int status = 1;
    std::string create_time;
};

template <typename T>
struct PageResult {
    std::vector<T> items;
    int total = 0;
    int page = 1;
    int size = 20;
    int pages = 0;
};

inline int calc_pages(int total, int size) {
    if (size <= 0) return 0;
    return (total + size - 1) / size;
}

}
