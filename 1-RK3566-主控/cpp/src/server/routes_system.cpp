// =============================================================================
// routes_system.cpp —— 系统类路由
// =============================================================================
// 包括：健康检查、任务队列状态、打印机测试、操作日志、语音代理。
// =============================================================================

#include <cstdlib>
#include <fstream>     // 读静态文件
#include <sstream>

#include <nlohmann/json.hpp>

#include "common/config.h"
#include "common/http.h"
#include "common/http_client.h"
#include "common/json_util.h"
#include "common/log.h"
#include "core/printer.h"
#include "core/task_queue.h"
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

// 读整个文件到字符串（用于发静态页面）
bool read_file(const std::string& path, std::string& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f.is_open()) return false;

    std::ostringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}

}  // namespace

void register_system_routes(http::Server& srv, core::TaskQueue& queue) {

    srv.get("/api/health", [](const http::Request&, http::Response& resp) {
        resp.json_ok(json{
            {"status", "ok"},
            {"mode", "cpp"},
            {"db", db::current_path()},
        });
    });

    srv.get("/api/task-queue/status",
            [&queue](const http::Request&, http::Response& resp) {
        auto st = queue.status();

        json tasks = json::array();
        for (const auto& t : st.queued) {
            json item;
            item["action"] = t.first;
            item["coord_y"] = t.second;
            tasks.push_back(item);
        }

        json current = nullptr;
        if (!st.current_action.empty()) {
            current = json{{"action", st.current_action},
                           {"coord_y", st.current_coord}};
        }

        json data;
        data["pending"] = st.pending;
        data["current"] = current;
        data["tasks"] = tasks;
        resp.json_ok(data);
    });

    srv.post("/api/printer/test", [](const http::Request& req, http::Response& resp) {
        if (!require_admin(req, resp)) return;

        auto body = req.json();
        std::string title = body ? jstr(*body, "title", "测试标签") : "测试标签";

        bool ok = core::print_label("TEST", title);
        if (!ok) {
            resp.json_error(500, "打印指令发送失败，请检查打印机连接");
            return;
        }
        resp.json_ok("测试标签已发送");
    });

    srv.post("/api/printer/calibrate", [](const http::Request& req, http::Response& resp) {
        if (!require_admin(req, resp)) return;

        bool ok = core::printer_calibrate();
        if (!ok) {
            resp.json_error(500, "校准指令发送失败");
            return;
        }
        resp.json_ok("校准指令已发送（会走一段纸）");
    });

    srv.post("/api/operation-log/page", [](const http::Request& req, http::Response& resp) {
        if (!require_admin(req, resp)) return;

        auto body = req.json();
        json b = body ? *body : json::object();

        int page = static_cast<int>(jint(b, "page", 1));
        int size = static_cast<int>(jint(b, "size", 20));
        if (page < 1) page = 1;
        if (size < 1) size = 20;

        try {
            auto result = db::log_page(page, size);
            resp.json_ok(json_util::page_to_json(result, json_util::log_to_json));
        } catch (const std::exception& e) {
            resp.json_error(400, e.what());
        }
    });

    srv.get("/api/operation-log/by-instance/:id",
            [](const http::Request& req, http::Response& resp) {
        int64_t id = req.path_int("id");
        try {
            auto logs = db::log_by_instance(id);
            resp.json_ok(json_util::list_to_json(logs, json_util::log_to_json));
        } catch (const std::exception& e) {
            resp.json_error(400, e.what());
        }
    });

    srv.post("/api/admin/exec", [](const http::Request& req, http::Response& resp) {
        if (!require_admin(req, resp)) return;
        resp.json_error(501, "该接口在当前版本中未实现（安全考虑）");
    });

    LOG_INFO("[路由] 系统路由已注册");
}

}  // namespace routes
