// =============================================================================
// models.h —— 数据结构定义（对应数据库里的表）
// =============================================================================
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace models {

struct BookInfo {
    int64_t id = 0;                    // 主键（雪花算法生成的 64 位 ID）
    std::string title;                 // 书名（必填）
    std::string author;                // 作者
    std::string category;              // 分类：教辅 / 文学 / 科技 / 其他
    std::string cover_url;             // 封面图地址
    std::string description;           // 简介
    std::string create_time;           // 创建时间
    std::string update_time;           // 最后修改时间
    int is_deleted = 0;                // 软删除标记：1=已删（数据还在，只是查不到）
};

struct BookInstance {
    int64_t instance_id = 0;           // 主键
    int64_t book_id = 0;               // 指向 BookInfo.id
    int64_t user_id = 0;               // 保留字段（当前业务主要用 borrower_id）
    int64_t slot_id = 0;               // 占用哪个书架格子；0 = 没占格子
    int status = 0;                    // 状态：0待贴标 / 1已借出 / 2待上架 / 3在架
    int64_t borrower_id = 0;           // 借书人的用户 ID；0 = 没人借
    std::string borrow_time;           // 借出时间
    std::string return_time;           // 归还时间
    std::string due_date;              // 应还日期（借出时算好 = 借出时间 + 30 天）
    std::string create_time;
    std::string update_time;
    int is_deleted = 0;

    // ---- 以下为 JOIN 带出的冗余字段 ----
    std::string title;                 // ⭐ 书名（JOIN book_info）
    std::string category;              // ⭐ 分类（JOIN book_info）
    int coord_x = 0;                   // ⭐ 层号（JOIN storage_slot）
    int coord_y = 0;                   // ⭐ 位置号（JOIN storage_slot），如 15 表示 1 层 5 号
    std::string remark;                // ⭐ 槽位备注，如 "文学区-1"
    std::string borrower_name;         // ⭐ 借阅人昵称（JOIN user）
};

struct StorageSlot {
    int64_t slot_id = 0;               // 主键（自增）
    int coord_x = 0;                   // 层号：1 或 2
    int coord_y = 0;                   // 位置号：11-14 / 15-18 / 21-24 / 25-28（两位数编码）
    int is_occupied = 0;               // 0 = 空格子，1 = 已被占用
    std::string remark;                // 备注，如 "文学区-1"，分配时按这个前缀找同分类格子
    std::string create_time;
    std::string update_time;
    int is_deleted = 0;

    // ---- JOIN 字段 ----
    int64_t instance_id = 0;           // ⭐ 占着这个格子的书本实例 ID（0 = 没有）
    std::string title;                 // ⭐ 格子里的书名
};

struct User {
    int64_t user_id = 0;               // 主键
    std::string openid;                // 微信小程序的用户唯一标识（可为空）
    std::string nickname;              // 昵称
    std::string avatar_url;            // 头像地址
    int role = 0;                      // 角色：0普通用户 / 1管理员
    int is_frozen = 0;                 // 是否冻结（逾期未还会被冻结，冻结后不能借书）
    double fine_balance = 0.0;         // 欠缴罚款金额（元）
    std::string create_time;
    std::string update_time;
    int is_deleted = 0;

    // ---- 统计字段 ----
    int borrow_count = 0;              // ⭐ 当前借了几本书（子查询统计）
};

struct OperationLog {
    int64_t log_id = 0;
    int64_t instance_id = 0;           // 关联的书本实例
    int64_t user_id = 0;               // 操作人
    std::string action_type;           // 动作类型：CREATE / BORROW / RETURN / SHELVE / UNSHELVE ...
    std::string content;               // 日志正文
    std::string operator_name;         // 操作人名字（冗余存一份：用户改名后历史日志仍显示当时的名字）
    int status = 1;                    // 1 = 成功，0 = 失败
    std::string create_time;
};

template <typename T>
struct PageResult {
    std::vector<T> items;              // 当前页的数据
    int total = 0;                     // 总记录数
    int page = 1;                      // 当前第几页（从 1 开始）
    int size = 20;                     // 每页几条
    int pages = 0;                     // 总页数 = ceil(total / size)
};

inline int calc_pages(int total, int size) {
    if (size <= 0) return 0;
    return (total + size - 1) / size;
}

}  // namespace models
