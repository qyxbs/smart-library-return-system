// =============================================================================
// routes_k230.cpp —— K230 相关 + 管理员上架/下架 + 输入法
// =============================================================================

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>

#include <nlohmann/json.hpp>

#include "common/config.h"
#include "common/http.h"
#include "common/http_client.h"
#include "common/json_util.h"
#include "common/log.h"
#include "core/task_queue.h"
#include "data/db.h"
#include "server/routes.h"

namespace routes {

using nlohmann::json;

namespace {

std::string bridge_cmd(const std::string& command, int timeout_ms = 5000) {
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return "";

    // 超时
    timeval tv{};
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    ::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(cfg::BRIDGE_CMD_PORT));
    ::inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

    if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        ::close(fd);
        LOG_WARN("[IPC] 连不上桥接进程（", std::strerror(errno), "）");
        return "";
    }

    std::string out = command + "\n";
    ::send(fd, out.data(), out.size(), MSG_NOSIGNAL);

    // 读一行回复
    std::string reply;
    char c;
    while (::recv(fd, &c, 1, 0) > 0) {
        if (c == '\n') break;
        if (c != '\r') reply += c;
        if (reply.size() > 8192) break;
    }
    ::close(fd);
    return reply;
}

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

void register_k230_routes(http::Server& srv, core::TaskQueue& queue) {

    srv.get("/api/k230/status", [](const http::Request&, http::Response& resp) {
        std::string r = bridge_cmd("STATUS", 2000);

        if (r.empty()) {
            // 桥接不在线
            json data;
            data["bridge_online"] = false;
            data["k2301_connected"] = false;
            data["message"] = "K230 桥接进程未运行";
            resp.json_ok(data);
            return;
        }

        // 桥接回的是 "k2301=1 scanned=5 last=123456"
        json data;
        data["bridge_online"] = true;
        data["raw"] = r;
        data["k2301_connected"] = (r.find("k2301=1") != std::string::npos);
        resp.json_ok(data);
    });

    srv.post("/api/k230/wake", [](const http::Request& req, http::Response& resp) {
        if (!require_admin(req, resp)) return;

        std::string r = bridge_cmd("ADMIN:WAKE");
        if (r.empty()) {
            resp.json_error(503, "K230 桥接进程不可达");
            return;
        }
        if (r == "ACK") {
            resp.json_ok("WAKE 已发送给 K230#1");
        } else {
            resp.json_error(503, "桥接回复: " + r);
        }
    });

    srv.post("/api/k230/sleep", [](const http::Request& req, http::Response& resp) {
        if (!require_admin(req, resp)) return;

        std::string r = bridge_cmd("ADMIN:SLEEP");
        if (r.empty()) {
            resp.json_error(503, "K230 桥接进程不可达");
            return;
        }
        if (r == "ACK") {
            resp.json_ok("SLEEP 已发送给 K230#1");
        } else {
            resp.json_error(503, "桥接回复: " + r);
        }
    });

    srv.get("/api/k230/lookup/:book_id",
            [](const http::Request& req, http::Response& resp) {
        int64_t book_id = req.path_int("book_id");

        auto inst = db::instance_get_on_shelf_by_book_id(book_id);
        if (!inst) {
            inst = db::instance_get(book_id);
        }

        if (!inst) {
            resp.json_error(404, "书本不存在");
            return;
        }

        json data;
        data["instance_id"] = json_util::safe_int(inst->instance_id);
        data["book_id"]     = json_util::safe_int(inst->book_id);
        data["title"]       = inst->title;
        data["status"]      = inst->status;
        data["coord_y"]     = inst->coord_y;
        data["remark"]      = inst->remark;
        data["y"]           = inst->coord_y / 10;   // AprilTag Y
        data["x"]           = inst->coord_y % 10;   // AprilTag X

        resp.json_ok(data);
    });

    srv.post("/api/k230/check-shelf", [](const http::Request& req, http::Response& resp) {
        if (!require_admin(req, resp)) return;

        std::string r = bridge_cmd("CHECK_SHELF", 8000);
        if (r.empty()) {
            resp.json_error(503, "K230 桥接进程不可达");
            return;
        }

        json data;
        if (r == "FAFA") {
            data["occupied"] = true;
            data["message"] = "取书区有书，请先取走";
        } else if (r == "FBFB") {
            data["occupied"] = false;
            data["message"] = "取书区为空，可以借书";
        } else {
            data["occupied"] = false;
            data["message"] = "状态未知: " + r;
        }
        resp.json_ok(data);
    });

    // POST /api/admin/start-shelve
    srv.post("/api/admin/start-shelve",
             [](const http::Request& req, http::Response& resp) {
        if (!require_admin(req, resp)) return;

        std::string r = bridge_cmd("ADMIN_SHELVE");
        if (r.empty()) {
            resp.json_error(503, "K230 桥接进程不可达（批量上架需要 K230#2）");
            return;
        }
        if (r == "OK") {
            json data;
            data["state"] = "started";
            data["message"] = "上架流程已启动";
            resp.json_ok(data);
        } else {
            resp.json_error(503, "桥接回复: " + r);
        }
    });

    // GET /api/admin/shelve-status
    srv.get("/api/admin/shelve-status",
            [](const http::Request&, http::Response& resp) {
        std::string r = bridge_cmd("ADMIN_SHELVE_STATUS", 3000);

        if (r.empty() || r.compare(0, 6, "ERROR:") == 0) {
            json data;
            data["state"] = "idle";
            data["message"] = "K230 桥接离线";
            resp.json_ok(data);
            return;
        }

        try {
            resp.json_ok(json::parse(r));
        } catch (...) {
            json data;
            data["state"] = "unknown";
            data["raw"] = r;
            resp.json_ok(data);
        }
    });

    // POST /api/book-instance/admin-shelve —— 单本上架
    srv.post("/api/book-instance/admin-shelve",
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
                           "管理员上架《" + inst->title + "》→ " + alloc.remark, 1);

            json data;
            data["instanceId"] = json_util::safe_int(instance_id);
            data["slotId"]     = json_util::safe_int(alloc.slot_id);
            data["coordY"]     = alloc.coord_y;
            data["remark"]     = alloc.remark;

            json out;
            out["code"] = 200;
            out["data"] = data;
            if (!task->ok) out["warning"] = "UART指令发送失败，请检查机械爪";
            resp.body = out.dump();

        } catch (const std::exception& e) {
            resp.json_error(400, e.what());
        }
    });

    // POST /api/book-instance/admin-unshelve —— 单本下架
    srv.post("/api/book-instance/admin-unshelve",
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

            // 释放格子 + 状态回到待贴标
            db::slot_release(inst->slot_id);
            db::InstanceUpdate upd;
            upd.slot_id = static_cast<int64_t>(0);
            upd.status = cfg::ST_PENDING_STICKER;
            db::instance_update(instance_id, upd);

            db::log_create(instance_id, 0, "UNSHELVE",
                           "管理员下架《" + inst->title + "》", 1);

            json out;
            out["code"] = 200;
            out["data"] = "已下架";
            if (!task->ok) out["warning"] = "UART指令发送失败，请检查机械爪";
            resp.body = out.dump();

        } catch (const std::exception& e) {
            resp.json_error(400, e.what());
        }
    });

    srv.get("/api/ime/pinyin", [](const http::Request& req, http::Response& resp) {
        std::string q = req.get("q");
        std::string prev = req.get("prev");

        if (q.empty()) {
            resp.json_ok(json{{"candidates", ""}});
            return;
        }

        std::string url = std::string(cfg::VOICE_SERVICE_URL) +
                          "/api/ime/pinyin?q=" + q +
                          (prev.empty() ? "" : "&prev=" + prev);

        auto r = http::get(url, 3000);
        if (r.ok()) {
            resp.status = r.status;
            resp.body = r.body;
            resp.content_type = "application/json; charset=utf-8";
            return;
        }

        LOG_DEBUG("[API] 拼音反查不可用，返回空候选");
        resp.json_ok(json{{"candidates", ""}});
    });

    LOG_INFO("[路由] K230/管理员/输入法路由已注册");
}

}  // namespace routes
