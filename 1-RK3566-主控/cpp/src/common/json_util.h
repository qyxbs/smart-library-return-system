// =============================================================================
// json_util.h —— 数据模型 ↔ JSON 转换
// =============================================================================
#pragma once

#include <cstdint>
#include <string>

#include <nlohmann/json.hpp>

#include "data/models.h"

namespace json_util {

// JS 能精确表示的最大整数：2^53
constexpr int64_t JS_SAFE_INTEGER = 9007199254740992LL;

inline nlohmann::json safe_int(int64_t v) {
    if (v > JS_SAFE_INTEGER || v < -JS_SAFE_INTEGER) {
        return std::to_string(v);   // 转成字符串，例如 "7484681093755318273"
    }
    return v;                       // 保持数字
}

inline nlohmann::json book_to_json(const models::BookInfo& b) {
    return nlohmann::json{
        {"id",          safe_int(b.id)},
        {"title",       b.title},
        {"author",      b.author},
        {"category",    b.category},
        {"cover_url",   b.cover_url},
        {"description", b.description},
        {"create_time", b.create_time},
        {"update_time", b.update_time},
    };
}

inline nlohmann::json instance_to_json(const models::BookInstance& x) {
    return nlohmann::json{
        {"instance_id",   safe_int(x.instance_id)},
        {"book_id",       safe_int(x.book_id)},
        {"user_id",       safe_int(x.user_id)},
        {"slot_id",       safe_int(x.slot_id)},
        {"status",        x.status},
        {"borrower_id",   safe_int(x.borrower_id)},
        {"borrow_time",   x.borrow_time},
        {"return_time",   x.return_time},
        {"due_date",      x.due_date},
        {"create_time",   x.create_time},
        {"update_time",   x.update_time},
        // ---- JOIN 字段 ----
        {"title",         x.title},
        {"category",      x.category},
        {"coord_x",       x.coord_x},
        {"coord_y",       x.coord_y},
        {"remark",        x.remark},
        {"borrower_name", x.borrower_name},
    };
}

// 书架格子
inline nlohmann::json slot_to_json(const models::StorageSlot& s) {
    return nlohmann::json{
        {"slot_id",     safe_int(s.slot_id)},
        {"coord_x",     s.coord_x},
        {"coord_y",     s.coord_y},
        {"is_occupied", s.is_occupied},
        {"remark",      s.remark},
        // 占着这个格子的书（没书时为空）
        {"instance_id", safe_int(s.instance_id)},
        {"title",       s.title},
    };
}

// 用户
inline nlohmann::json user_to_json(const models::User& u) {
    return nlohmann::json{
        {"user_id",      safe_int(u.user_id)},
        {"openid",       u.openid},
        {"nickname",     u.nickname},
        {"avatar_url",   u.avatar_url},
        {"role",         u.role},
        {"is_frozen",    u.is_frozen},
        {"fine_balance", u.fine_balance},
        {"borrow_count", u.borrow_count},
        {"create_time",  u.create_time},
        {"update_time",  u.update_time},
    };
}

// 操作日志
inline nlohmann::json log_to_json(const models::OperationLog& l) {
    return nlohmann::json{
        {"log_id",        safe_int(l.log_id)},
        {"instance_id",   safe_int(l.instance_id)},
        {"user_id",       safe_int(l.user_id)},
        {"action_type",   l.action_type},
        {"content",       l.content},
        {"status",        l.status},
        {"create_time",   l.create_time},
        {"operator_name", l.operator_name},
    };
}

template <typename T, typename ToJsonFn>
inline nlohmann::json page_to_json(const models::PageResult<T>& p,
                                   ToJsonFn to_json,
                                   const std::string& item_key = "records") {
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& item : p.items) {
        arr.push_back(to_json(item));
    }

    return nlohmann::json{
        {"total",   p.total},
        {"page",    p.page},
        {"size",    p.size},
        {"pages",   p.pages},
        {item_key,  arr},
    };
}

// 列表序列化（不分页）
template <typename T, typename ToJsonFn>
inline nlohmann::json list_to_json(const std::vector<T>& items, ToJsonFn to_json) {
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& item : items) {
        arr.push_back(to_json(item));
    }
    return arr;
}

}  // namespace json_util
