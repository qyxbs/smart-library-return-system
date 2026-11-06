// =============================================================================
// routes_user.cpp —— 用户 / 鉴权 / 罚款 相关路由
// =============================================================================

#include <nlohmann/json.hpp>

#include "common/config.h"
#include "common/http.h"
#include "common/json_util.h"
#include "common/log.h"
#include "data/db.h"
#include "server/routes.h"

namespace routes {

using nlohmann::json;

namespace {

std::string jstr(const json& j, const std::string& key, const std::string& def = "") {
    if (!j.contains(key) || !j[key].is_string()) return def;
    return j[key].get<std::string>();
}

int64_t jint(const json& j, const std::string& key, int64_t def = 0) {
    if (!j.contains(key)) return def;
    if (j[key].is_number_integer()) return j[key].get<int64_t>();
    if (j[key].is_string()) {
        try { return std::stoll(j[key].get<std::string>()); } catch (...) { return def; }
    }
    return def;
}

}  // namespace

void register_user_routes(http::Server& srv) {

    srv.post("/api/users/login", [](const http::Request& req, http::Response& resp) {
        auto body = req.json();
        if (!body) {
            resp.json_error(400, "请求正文不是合法 JSON");
            return;
        }

        std::string openid = jstr(*body, "openid");
        if (openid.empty()) {
            resp.json_error(400, "缺少 openid");
            return;
        }

        try {
            // 先查有没有这个用户
            auto existing = db::user_get_by_openid(openid);

            if (existing) {
                // 老用户：顺便更新一下昵称/头像（微信那边可能改了）
                std::string nickname = jstr(*body, "nickname");
                std::string avatar   = jstr(*body, "avatarUrl", jstr(*body, "avatar_url"));

                if (!nickname.empty() || !avatar.empty()) {
                    db::UserUpdate upd;
                    if (!nickname.empty()) upd.nickname = nickname;
                    if (!avatar.empty())   upd.avatar_url = avatar;
                    db::user_update(existing->user_id, upd);
                    existing = db::user_get(existing->user_id);
                }

                json data = json_util::user_to_json(*existing);
                data["token"] = "local_" + openid;   // 前端存这个，之后每次带上
                resp.json_ok(data);
            } else {
                // 新用户：创建
                auto created = db::user_create(
                    openid,
                    jstr(*body, "nickname", "微信用户"),
                    jstr(*body, "avatarUrl", jstr(*body, "avatar_url")),
                    cfg::ROLE_USER);

                json data = json_util::user_to_json(created);
                data["token"] = "local_" + openid;
                resp.json_ok(data);
            }
        } catch (const std::exception& e) {
            LOG_WARN("[API] 登录失败: ", e.what());
            resp.json_error(400, e.what());
        }
    });

    srv.get("/api/users/me", [](const http::Request& req, http::Response& resp) {
        auto u = current_user(req);
        if (!u) {
            resp.json_error(401, "请先登录");
            return;
        }
        auto full = db::user_get(u->user_id);
        if (!full) {
            resp.json_error(404, "用户不存在");
            return;
        }
        resp.json_ok(json_util::user_to_json(*full));
    });

    srv.post("/api/users/page", [](const http::Request& req, http::Response& resp) {
        if (!require_admin(req, resp)) return;

        auto body = req.json();
        json b = body ? *body : json::object();

        int page = static_cast<int>(jint(b, "page", 1));
        int size = static_cast<int>(jint(b, "size", 20));
        if (page < 1) page = 1;
        if (size < 1) size = 20;

        try {
            auto result = db::user_list(page, size);
            resp.json_ok(json_util::page_to_json(result, json_util::user_to_json));
        } catch (const std::exception& e) {
            resp.json_error(400, e.what());
        }
    });

    srv.put("/api/users/:id", [](const http::Request& req, http::Response& resp) {
        auto me = current_user(req);
        if (!me) {
            resp.json_error(401, "请先登录");
            return;
        }

        int64_t target_id = req.path_int("id");

        // 只能改自己，或者管理员改别人
        if (me->user_id != target_id && me->role != cfg::ROLE_ADMIN) {
            resp.json_error(403, "无权修改他人信息");
            return;
        }

        auto body = req.json();
        if (!body) {
            resp.json_error(400, "请求正文不是合法 JSON");
            return;
        }

        db::UserUpdate upd;
        if (body->contains("nickname"))   upd.nickname = jstr(*body, "nickname");
        if (body->contains("avatar_url")) upd.avatar_url = jstr(*body, "avatar_url");
        if (body->contains("avatarUrl"))  upd.avatar_url = jstr(*body, "avatarUrl");
        // 【注意】这里故意不处理 role 字段

        if (!db::user_update(target_id, upd)) {
            resp.json_error(404, "用户不存在或无字段可更新");
            return;
        }

        auto u = db::user_get(target_id);
        resp.json_ok(json_util::user_to_json(*u));
    });

    srv.post("/api/users/promote", [](const http::Request& req, http::Response& resp) {
        auto body = req.json();
        if (!body) {
            resp.json_error(400, "请求正文不是合法 JSON");
            return;
        }

        int64_t user_id = jint(*body, "userId");
        std::string pin = jstr(*body, "pin");

        // PIN 可以从环境变量覆盖，默认 123456
        const char* env_pin = std::getenv("ADMIN_PIN");
        std::string correct_pin = env_pin ? env_pin : "123456";

        if (pin != correct_pin) {
            resp.json_error(403, "PIN 码错误");
            return;
        }

        db::UserUpdate upd;
        upd.role = cfg::ROLE_ADMIN;
        if (!db::user_update(user_id, upd)) {
            resp.json_error(404, "用户不存在");
            return;
        }

        LOG_INFO("[API] 用户 ", user_id, " 提升为管理员");
        resp.json_ok("已提升为管理员");
    });

    srv.post("/api/users/demote", [](const http::Request& req, http::Response& resp) {
        auto body = req.json();
        if (!body) {
            resp.json_error(400, "请求正文不是合法 JSON");
            return;
        }

        std::string pin = jstr(*body, "pin");
        const char* env_pin = std::getenv("ADMIN_PIN");
        std::string correct_pin = env_pin ? env_pin : "123456";
        if (pin != correct_pin) {
            resp.json_error(403, "PIN 码错误");
            return;
        }

        int64_t user_id = jint(*body, "userId");
        db::UserUpdate upd;
        upd.role = cfg::ROLE_USER;
        if (!db::user_update(user_id, upd)) {
            resp.json_error(404, "用户不存在");
            return;
        }

        LOG_INFO("[API] 用户 ", user_id, " 已降级（声纹文件由语音服务管理）");
        resp.json_ok("已降级");
    });

    srv.post("/api/users/freeze", [](const http::Request& req, http::Response& resp) {
        auto body = req.json();
        if (!body) {
            resp.json_error(400, "请求正文不是合法 JSON");
            return;
        }

        std::string pin = jstr(*body, "pin");
        const char* env_pin = std::getenv("ADMIN_PIN");
        std::string correct_pin = env_pin ? env_pin : "123456";
        if (pin != correct_pin) {
            resp.json_error(403, "PIN 码错误");
            return;
        }

        int64_t user_id = jint(*body, "userId");
        int frozen = static_cast<int>(jint(*body, "frozen", 0));

        db::UserUpdate upd;
        upd.is_frozen = frozen;
        if (!db::user_update(user_id, upd)) {
            resp.json_error(404, "用户不存在");
            return;
        }

        LOG_INFO("[API] 用户 ", user_id, frozen ? "已冻结" : "已解冻");
        resp.json_ok(frozen ? "已冻结" : "已解冻");
    });

    srv.post("/api/users/pay-fine", [](const http::Request& req, http::Response& resp) {
        auto me = current_user(req);
        if (!me) {
            resp.json_error(401, "请先登录");
            return;
        }

        auto body = req.json();
        int64_t user_id = body ? jint(*body, "userId", me->user_id) : me->user_id;

        // 只能交自己的，除非是管理员
        if (user_id != me->user_id && me->role != cfg::ROLE_ADMIN) {
            resp.json_error(403, "无权操作他人账户");
            return;
        }

        auto u = db::user_get(user_id);
        if (!u) {
            resp.json_error(404, "用户不存在");
            return;
        }

        double fine = u->fine_balance;
        if (fine <= 0) {
            resp.json_ok(json{{"message", "没有待缴罚款"}, {"paid", 0.0}});
            return;
        }

        // 清零罚款：直接设成 0（用负数抵扣）
        db::user_add_fine(user_id, -fine);

        db::log_create(0, user_id, "PAY_FINE",
                       "缴纳罚款 ¥" + std::to_string(fine), 1,
                       u->nickname);

        LOG_INFO("[API] 用户 ", user_id, " 缴纳罚款 ", fine, " 元");

        json data;
        data["message"] = "已缴纳 ¥" + std::to_string(fine) + "，欠费已结清";
        data["paid"] = fine;
        resp.json_ok(data);
    });

    LOG_INFO("[路由] 用户路由已注册");
}

}  // namespace routes
