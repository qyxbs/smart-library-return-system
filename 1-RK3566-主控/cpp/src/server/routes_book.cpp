// =============================================================================
// routes_book.cpp —— 书目 / 书本实例 / 书架格子 相关路由
// =============================================================================

#include <nlohmann/json.hpp>

#include <cstdio>
#include <cstdlib>

#include "common/config.h"
#include "common/http.h"
#include "common/http_client.h"
#include "common/json_util.h"
#include "common/log.h"
#include "core/task_queue.h"
#include "core/printer.h"
#include "data/db.h"
#include "server/routes.h"

namespace routes {

using nlohmann::json;

namespace {

// 轻量 token 签名：FNV-1a + 服务端密钥（部署时用 BOOK_TOKEN_SECRET 覆盖）
uint64_t fnv1a64(const std::string& s, uint64_t h = 1469598103934665603ULL) {
    for (unsigned char c : s) {
        h ^= c;
        h *= 1099511628211ULL;
    }
    return h;
}

const std::string& token_secret() {
    static const std::string s = [] {
        const char* env = std::getenv("BOOK_TOKEN_SECRET");
        return std::string(env && *env ? env : "book-local-secret-change-me");
    }();
    return s;
}

std::string sig_of(const std::string& openid) {
    char buf[17];
    std::snprintf(buf, sizeof(buf), "%016llx",
                  static_cast<unsigned long long>(fnv1a64(openid + "|" + token_secret())));
    return std::string(buf);
}

}  // namespace

std::string make_token(const std::string& openid) {
    return "local_" + openid + "." + sig_of(openid);
}

std::optional<AuthUser> current_user(const http::Request& req) {
    auto it = req.headers.find("authorization");
    if (it == req.headers.end()) {
        return std::nullopt;
    }

    std::string token = it->second;
    // 去掉 "Bearer " 前缀
    const std::string bearer = "Bearer ";
    if (token.compare(0, bearer.size(), bearer) == 0) {
        token = token.substr(bearer.size());
    }

    const std::string prefix = "local_";
    if (token.compare(0, prefix.size(), prefix) != 0) {
        return std::nullopt;
    }

    // token 形如 local_<openid>.<sig>：校验签名，避免客户端自行构造
    auto dot = token.rfind('.');
    if (dot == std::string::npos || dot <= prefix.size()) {
        return std::nullopt;
    }
    std::string openid = token.substr(prefix.size(), dot - prefix.size());
    if (token.substr(dot + 1) != sig_of(openid)) {
        return std::nullopt;
    }
    auto u = db::user_get_by_openid(openid);
    if (!u) {
        return std::nullopt;
    }

    AuthUser au;
    au.user_id = u->user_id;
    au.openid = u->openid;
    au.role = u->role;
    return au;
}

bool require_admin(const http::Request& req, http::Response& resp) {
    auto u = current_user(req);
    if (!u) {
        resp.json_error(401, "请先登录");
        return false;
    }
    if (u->role != cfg::ROLE_ADMIN) {
        resp.json_error(403, "仅管理员可操作");
        return false;
    }
    return true;
}

namespace {

// 取字符串字段（不存在或类型不对就返回默认值）
std::string jstr(const json& j, const std::string& key, const std::string& def = "") {
    if (!j.contains(key) || !j[key].is_string()) return def;
    return j[key].get<std::string>();
}

int64_t jint(const json& j, const std::string& key, int64_t def = 0) {
    if (!j.contains(key)) return def;

    if (j[key].is_number_integer()) {
        return j[key].get<int64_t>();
    }
    if (j[key].is_number_unsigned()) {
        return static_cast<int64_t>(j[key].get<uint64_t>());
    }
    if (j[key].is_string()) {
        try {
            return std::stoll(j[key].get<std::string>());
        } catch (...) {
            return def;
        }
    }
    return def;
}

std::string trim(const std::string& s) {
    size_t b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return "";
    size_t e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

// 把异常转成统一的错误响应
void handle_error(const std::exception& e, http::Response& resp,
                  const std::string& where) {
    LOG_WARN("[API] ", where, " 失败: ", e.what());
    resp.json_error(400, e.what());
}

}  // namespace

void register_book_routes(http::Server& srv, core::TaskQueue& queue) {

    srv.post("/api/book/add", [](const http::Request& req, http::Response& resp) {
        if (!require_admin(req, resp)) return;

        auto body = req.json();
        if (!body) {
            resp.json_error(400, "请求正文不是合法 JSON");
            return;
        }

        std::string title = trim(jstr(*body, "title"));
        std::string category = trim(jstr(*body, "category"));

        if (title.empty()) {
            resp.json_error(400, "书名为空");
            return;
        }

        // ---- 分类为空时自动分类 ----
        if (category.empty()) {
            json classify_req;
            classify_req["title"] = title;
            classify_req["force_local"] = body->value("force_local", false);

            auto r = http::post_json(std::string(cfg::VOICE_SERVICE_URL) + "/api/classify",
                                     classify_req.dump(), 15000);
            if (r.ok()) {
                try {
                    auto cr = json::parse(r.body);
                    if (cr.value("ok", false) && cr.contains("category")) {
                        category = cr["category"].get<std::string>();
                    }
                } catch (...) {
                    // 解析失败就走默认分类
                }
            }
            if (category.empty()) {
                category = "其他";
                LOG_WARN("[API] 自动分类失败，使用默认分类\"其他\"");
            }
        }

        try {
            // ---- 创建书目 ----
            auto book = db::book_create(title, category);

            // ---- 打印标签 ----
            bool printed = core::print_label(std::to_string(book.id), title);

            // ---- 创建实例（待贴标）----
            auto inst = db::instance_create(book.id, 0, 0, cfg::ST_PENDING_STICKER);

            // ---- 写日志 ----
            db::log_create(inst.instance_id, 0, "PRINT",
                           "录入《" + title + "》 分类:" + category, 1);

            json data = "录入成功：" + title + "（" + category + "）";
            json out;
            out["code"] = 200;
            out["data"] = data;
            if (!printed) {
                out["warning"] = "打印指令发送失败，请检查打印机";
            }
            resp.body = out.dump();
            resp.status = 200;

        } catch (const std::exception& e) {
            handle_error(e, resp, "录入书目");
        }
    });

    srv.get("/api/book/search", [](const http::Request& req, http::Response& resp) {
        std::string keyword = req.get("keyword");
        if (keyword.empty()) {
            resp.json_error(400, "缺少 keyword 参数");
            return;
        }

        try {
            auto books = db::book_search(keyword);
            resp.json_ok(json_util::list_to_json(books, json_util::book_to_json));
        } catch (const std::exception& e) {
            handle_error(e, resp, "搜索书目");
        }
    });

    srv.get("/api/book/device/status", [](const http::Request&, http::Response& resp) {
        resp.json_ok(true);
    });

    srv.post("/api/book-instance/page", [](const http::Request& req, http::Response& resp) {
        auto body = req.json();
        json b = body ? *body : json::object();

        int page = static_cast<int>(jint(b, "page", 1));
        int size = static_cast<int>(jint(b, "size", 20));
        if (page < 1) page = 1;
        if (size < 1) size = 20;
        if (size > 200) size = 200;   // 防止一次拉太多

        // status 可能是数字、字符串数字、逗号分隔、或 "overdue"
        std::string status_filter;
        if (b.contains("status")) {
            if (b["status"].is_string()) {
                status_filter = b["status"].get<std::string>();
            } else if (b["status"].is_number()) {
                status_filter = std::to_string(b["status"].get<int>());
            }
        }

        try {
            auto result = db::instance_page(page, size, status_filter);
            resp.json_ok(json_util::page_to_json(result, json_util::instance_to_json));
        } catch (const std::exception& e) {
            handle_error(e, resp, "分页查询实例");
        }
    });

    srv.get("/api/book-instance/shelf", [](const http::Request&, http::Response& resp) {
        try {
            auto list = db::instance_shelf_list();
            resp.json_ok(json_util::list_to_json(list, json_util::instance_to_json));
        } catch (const std::exception& e) {
            handle_error(e, resp, "查询书架");
        }
    });

    srv.get("/api/book-instance/my-borrowed",
            [](const http::Request& req, http::Response& resp) {
        int64_t user_id = 0;
        std::string s = req.get("userId");
        if (!s.empty()) {
            try { user_id = std::stoll(s); } catch (...) { user_id = 0; }
        }

        try {
            auto list = db::instance_borrowed_list(user_id);
            resp.json_ok(json_util::list_to_json(list, json_util::instance_to_json));
        } catch (const std::exception& e) {
            handle_error(e, resp, "查询借阅列表");
        }
    });

    srv.get("/api/book-instance/by-slot/:id",
            [](const http::Request& req, http::Response& resp) {
        int64_t slot_id = req.path_int("id");
        auto inst = db::instance_get_by_slot(slot_id);
        if (!inst) {
            resp.json_error(404, "该格子没有书");
            return;
        }
        resp.json_ok(json_util::instance_to_json(*inst));
    });

    srv.post("/api/book-instance/scan",
             [&queue](const http::Request& req, http::Response& resp) {
        if (!require_admin(req, resp)) return;

        auto body = req.json();
        if (!body) {
            resp.json_error(400, "请求正文不是合法 JSON");
            return;
        }

        int64_t book_id = jint(*body, "bookId");
        if (book_id == 0) {
            resp.json_error(400, "缺少 bookId");
            return;
        }

        try {
            // ---- 查分类 ----
            auto book = db::book_get(book_id);
            if (!book) {
                resp.json_error(404, "书目不存在");
                return;
            }

            // ---- 分配格子 ----
            auto alloc = db::scan_allocate(book_id, book->category, cfg::ST_ON_SHELF);

            auto task = queue.enqueue(core::PRIO_SHELVE, "shelve",
                                      alloc.coord_y, 30000);

            // ---- 写日志 ----
            db::log_create(alloc.instance_id, 0, "SHELVE",
                           "扫码入库《" + book->title + "》→ " + alloc.remark, 1);

            json data;
            data["instanceId"] = json_util::safe_int(alloc.instance_id);
            data["bookId"]     = json_util::safe_int(book_id);
            data["slotId"]     = json_util::safe_int(alloc.slot_id);
            data["coordX"]     = alloc.coord_x;
            data["coordY"]     = alloc.coord_y;
            data["remark"]     = alloc.remark;
            data["status"]     = cfg::ST_ON_SHELF;

            json out;
            out["code"] = 200;
            out["data"] = data;
            if (!task->ok) {
                out["warning"] = "UART指令发送失败，请检查机械爪";
            }
            resp.body = out.dump();
            resp.status = 200;

        } catch (const std::exception& e) {
            handle_error(e, resp, "扫码入库");
        }
    });

    srv.post("/api/book-instance/borrow",
             [&queue](const http::Request& req, http::Response& resp) {
        auto body = req.json();
        if (!body) {
            resp.json_error(400, "请求正文不是合法 JSON");
            return;
        }

        int64_t instance_id = jint(*body, "instanceId");
        int64_t user_id     = jint(*body, "userId");

        if (instance_id == 0 || user_id == 0) {
            resp.json_error(400, "缺少 instanceId 或 userId");
            return;
        }

        try {
            auto before = db::instance_get(instance_id);
            if (!before) {
                resp.json_error(404, "书本实例不存在");
                return;
            }
            int coord_y = before->coord_y;

            // 数据库操作（含全部规则校验）
            auto inst = db::borrow_instance(instance_id, user_id);

            // 机械爪取书
            auto task = queue.enqueue(core::PRIO_BORROW, "borrow", coord_y, 30000);

            db::log_create(instance_id, user_id, "BORROW",
                           "借阅《" + inst.title + "》", 1);

            json data = json_util::instance_to_json(inst);
            json out;
            out["code"] = 200;
            out["data"] = data;
            if (!task->ok) {
                out["warning"] = "UART指令发送失败，请检查机械爪";
            }
            resp.body = out.dump();
            resp.status = 200;

        } catch (const std::exception& e) {
            handle_error(e, resp, "借书");
        }
    });

    srv.post("/api/book-instance/return",
             [](const http::Request& req, http::Response& resp) {
        auto body = req.json();
        if (!body) {
            resp.json_error(400, "请求正文不是合法 JSON");
            return;
        }

        int64_t instance_id = jint(*body, "instanceId");
        if (instance_id == 0) {
            resp.json_error(400, "缺少 instanceId");
            return;
        }

        try {
            auto result = db::return_instance(instance_id);

            // 逾期自动累加罚款
            if (result.fine > 0) {
                auto inst = db::instance_get(instance_id);
                if (inst && inst->borrower_id > 0) {
                    db::user_add_fine(inst->borrower_id, result.fine);
                    LOG_INFO("[API] 逾期 ", result.overdue_days, " 天，罚款 ",
                             result.fine, " 元已记账");
                }
            }

            db::log_create(instance_id, 0, "RETURN",
                           "归还《" + result.instance.title + "》→ 待上架", 1);

            json data = json_util::instance_to_json(result.instance);
            data["overdue_days"] = result.overdue_days;
            data["overdue_fine"] = result.fine;

            resp.json_ok(data);

        } catch (const std::exception& e) {
            handle_error(e, resp, "还书");
        }
    });

    srv.post("/api/book-instance/re-shelve",
             [&queue](const http::Request& req, http::Response& resp) {
        if (!require_admin(req, resp)) return;

        auto body = req.json();
        if (!body) {
            resp.json_error(400, "请求正文不是合法 JSON");
            return;
        }

        int64_t instance_id = jint(*body, "instanceId");
        if (instance_id == 0) {
            resp.json_error(400, "缺少 instanceId");
            return;
        }

        try {
            auto inst = db::instance_get(instance_id);
            if (!inst) {
                resp.json_error(404, "书本实例不存在");
                return;
            }

            auto alloc = db::re_shelve_instance(instance_id, inst->category);
            auto task = queue.enqueue(core::PRIO_SHELVE, "shelve", alloc.coord_y, 30000);

            db::log_create(instance_id, 0, "SHELVE",
                           "重新上架《" + inst->title + "》→ " + alloc.remark, 1);

            json data;
            data["instanceId"] = json_util::safe_int(instance_id);
            data["slotId"]     = json_util::safe_int(alloc.slot_id);
            data["coordX"]     = alloc.coord_x;
            data["coordY"]     = alloc.coord_y;
            data["remark"]     = alloc.remark;
            data["status"]     = cfg::ST_ON_SHELF;

            json out;
            out["code"] = 200;
            out["data"] = data;
            if (!task->ok) out["warning"] = "UART指令发送失败，请检查机械爪";
            resp.body = out.dump();

        } catch (const std::exception& e) {
            handle_error(e, resp, "重新上架");
        }
    });

    srv.post("/api/book-instance/retrieve",
             [&queue](const http::Request& req, http::Response& resp) {
        if (!require_admin(req, resp)) return;

        auto body = req.json();
        if (!body) {
            resp.json_error(400, "请求正文不是合法 JSON");
            return;
        }

        int64_t instance_id = jint(*body, "instanceId");
        if (instance_id == 0) {
            resp.json_error(400, "缺少 instanceId");
            return;
        }

        try {
            auto inst = db::instance_get(instance_id);
            if (!inst) {
                resp.json_error(404, "书本实例不存在");
                return;
            }
            if (inst->slot_id == 0) {
                resp.json_error(400, "这本书不在书架上");
                return;
            }

            int coord_y = inst->coord_y;
            auto task = queue.enqueue(core::PRIO_RETRIEVE, "retrieve", coord_y, 30000);

            // 取走后释放格子、状态改为待贴标
            db::slot_release(inst->slot_id);
            db::InstanceUpdate upd;
            upd.slot_id = static_cast<int64_t>(0);
            upd.status = cfg::ST_PENDING_STICKER;
            db::instance_update(instance_id, upd);

            db::log_create(instance_id, 0, "RETRIEVE",
                           "取出《" + inst->title + "》", 1);

            json out;
            out["code"] = 200;
            out["data"] = "取书指令已发送";
            if (!task->ok) out["warning"] = "UART指令发送失败，请检查机械爪";
            resp.body = out.dump();

        } catch (const std::exception& e) {
            handle_error(e, resp, "取书");
        }
    });

    srv.post("/api/book-instance/verify",
             [](const http::Request& req, http::Response& resp) {
        if (!require_admin(req, resp)) return;

        auto body = req.json();
        int64_t instance_id = body ? jint(*body, "instanceId") : 0;
        if (instance_id == 0) {
            resp.json_error(400, "缺少 instanceId");
            return;
        }

        try {
            auto inst = db::instance_get(instance_id);
            if (!inst) {
                resp.json_error(404, "实例不存在");
                return;
            }

            // 释放格子 + 状态回到待贴标
            if (inst->slot_id != 0) {
                db::slot_release(inst->slot_id);
            }
            db::InstanceUpdate upd;
            upd.slot_id = static_cast<int64_t>(0);
            upd.status = cfg::ST_PENDING_STICKER;
            db::instance_update(instance_id, upd);

            db::log_create(instance_id, 0, "VERIFY", "回收确认", 1);
            resp.json_ok("已回收");

        } catch (const std::exception& e) {
            handle_error(e, resp, "回收确认");
        }
    });

    srv.get("/api/book-instance/:id",
            [](const http::Request& req, http::Response& resp) {
        int64_t id = req.path_int("id");
        auto inst = db::instance_get(id);
        if (!inst) {
            resp.json_error(404, "书本实例不存在");
            return;
        }
        resp.json_ok(json_util::instance_to_json(*inst));
    });

    srv.put("/api/book-instance/:id",
            [](const http::Request& req, http::Response& resp) {
        if (!require_admin(req, resp)) return;

        int64_t id = req.path_int("id");
        auto body = req.json();
        if (!body) {
            resp.json_error(400, "请求正文不是合法 JSON");
            return;
        }

        db::InstanceUpdate upd;
        if (body->contains("status"))     upd.status = static_cast<int>(jint(*body, "status"));
        if (body->contains("slot_id"))    upd.slot_id = jint(*body, "slot_id");
        if (body->contains("borrower_id")) upd.borrower_id = jint(*body, "borrower_id");
        if (body->contains("due_date"))   upd.due_date = jstr(*body, "due_date");

        bool ok = db::instance_update(id, upd);
        if (!ok) {
            resp.json_error(404, "书本实例不存在或无字段可更新");
            return;
        }

        auto inst = db::instance_get(id);
        resp.json_ok(json_util::instance_to_json(*inst));
    });

    srv.del("/api/book-instance/:id",
            [](const http::Request& req, http::Response& resp) {
        if (!require_admin(req, resp)) return;

        int64_t id = req.path_int("id");
        bool ok = db::instance_delete(id);
        if (!ok) {
            resp.json_error(404, "书本实例不存在");
            return;
        }
        resp.json_ok("已删除");
    });

    srv.post("/api/book-instance",
             [](const http::Request& req, http::Response& resp) {
        if (!require_admin(req, resp)) return;

        auto body = req.json();
        if (!body) {
            resp.json_error(400, "请求正文不是合法 JSON");
            return;
        }

        int64_t book_id = jint(*body, "bookId");
        if (book_id == 0) {
            resp.json_error(400, "缺少 bookId");
            return;
        }

        try {
            auto inst = db::instance_create(book_id, 0, 0,
                                            static_cast<int>(jint(*body, "status", 0)));
            resp.json_ok(json_util::instance_to_json(inst));
        } catch (const std::exception& e) {
            handle_error(e, resp, "创建实例");
        }
    });

    // GET /api/storage-slot/list —— 16 个格子的实时状态
    srv.get("/api/storage-slot/list", [](const http::Request&, http::Response& resp) {
        try {
            auto slots = db::slot_list();
            resp.json_ok(json_util::list_to_json(slots, json_util::slot_to_json));
        } catch (const std::exception& e) {
            handle_error(e, resp, "查询格子");
        }
    });

    // POST /api/storage-slot/list —— 同上（兼容前端用 POST）
    srv.post("/api/storage-slot/list", [](const http::Request&, http::Response& resp) {
        try {
            auto slots = db::slot_list();
            resp.json_ok(json_util::list_to_json(slots, json_util::slot_to_json));
        } catch (const std::exception& e) {
            handle_error(e, resp, "查询格子");
        }
    });

    // GET /api/storage-slot/:id
    srv.get("/api/storage-slot/:id",
            [](const http::Request& req, http::Response& resp) {
        int64_t id = req.path_int("id");
        auto slot = db::slot_get(id);
        if (!slot) {
            resp.json_error(404, "格子不存在");
            return;
        }
        resp.json_ok(json_util::slot_to_json(*slot));
    });

    // PUT /api/storage-slot/:id（管理员）
    srv.put("/api/storage-slot/:id",
            [](const http::Request& req, http::Response& resp) {
        if (!require_admin(req, resp)) return;

        int64_t id = req.path_int("id");
        auto body = req.json();
        if (!body) {
            resp.json_error(400, "请求正文不是合法 JSON");
            return;
        }

        db::SlotUpdate upd;
        if (body->contains("coord_x"))     upd.coord_x = static_cast<int>(jint(*body, "coord_x"));
        if (body->contains("coord_y"))     upd.coord_y = static_cast<int>(jint(*body, "coord_y"));
        if (body->contains("is_occupied")) upd.is_occupied = static_cast<int>(jint(*body, "is_occupied"));
        if (body->contains("remark"))      upd.remark = jstr(*body, "remark");

        if (!db::slot_update(id, upd)) {
            resp.json_error(404, "格子不存在或无字段可更新");
            return;
        }
        auto slot = db::slot_get(id);
        resp.json_ok(json_util::slot_to_json(*slot));
    });

    // POST /api/storage-slot —— 新建格子（管理员，一般用不到，初始化已建好 16 个）
    srv.post("/api/storage-slot",
             [](const http::Request& req, http::Response& resp) {
        if (!require_admin(req, resp)) return;
        resp.json_error(400, "格子由系统初始化，不支持手动新建");
    });

    LOG_INFO("[路由] 书目/实例/槽位路由已注册");
}

}  // namespace routes
