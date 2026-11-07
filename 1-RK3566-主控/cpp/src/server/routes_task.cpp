// routes_task.cpp —— 二维码任务 / 扫码回调 路由

#include <nlohmann/json.hpp>

#include "common/config.h"
#include "common/http.h"
#include "common/json_util.h"
#include "common/log.h"
#include "core/task_queue.h"
#include "core/task_store.h"
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

json task_to_json(const core::TaskRecord& t) {
    json j;
    j["task_id"]      = t.task_id;
    j["action"]       = t.action;
    j["user_id"]      = json_util::safe_int(t.user_id);
    j["status"]       = t.status;
    j["payload"]      = t.payload;
    j["result"]       = t.result;
    j["created_at"]   = t.created_at;
    j["expires_at"]   = t.expires_at;
    j["completed_at"] = t.completed_at;
    return j;
}

void execute_task(const core::TaskRecord& t, core::TaskQueue& queue) {
    try {
        if (t.action == "borrow") {
            int64_t instance_id = jint(t.payload, "instanceId");
            if (instance_id == 0) {
                core::store().fail(t.task_id, "缺少 instanceId");
                return;
            }

            auto before = db::instance_get(instance_id);
            int coord_y = before ? before->coord_y : 0;

            auto inst = db::borrow_instance(instance_id, t.user_id);

            auto task = queue.enqueue(core::PRIO_BORROW, "borrow", coord_y, 30000);

            db::log_create(instance_id, t.user_id, "BORROW",
                           "借阅《" + inst.title + "》", 1);

            json result;
            result["message"] = "借书成功";
            result["title"] = inst.title;
            result["due_date"] = inst.due_date;
            if (!task->ok) result["warning"] = "机械爪指令发送失败";

            core::store().complete(t.task_id, result);

        } else if (t.action == "return") {
            auto borrowed = db::instance_borrowed_list(t.user_id);
            if (borrowed.empty()) {
                core::store().fail(t.task_id, "没有借阅中的书");
                return;
            }

            int64_t instance_id = borrowed.front().instance_id;
            auto result = db::return_instance(instance_id);

            if (result.fine > 0) {
                db::user_add_fine(t.user_id, result.fine);
            }

            db::log_create(instance_id, t.user_id, "RETURN",
                           "归还《" + result.instance.title + "》", 1);

            json r;
            r["message"] = "还书成功";
            r["title"] = result.instance.title;
            r["overdue_days"] = result.overdue_days;
            r["overdue_fine"] = result.fine;

            core::store().complete(t.task_id, r);

        } else {
            core::store().fail(t.task_id, "未知动作: " + t.action);
        }

    } catch (const std::exception& e) {
        core::store().fail(t.task_id, e.what());
    }
}

}

void register_task_routes(http::Server& srv, core::TaskQueue& queue) {

    srv.post("/api/task/create", [](const http::Request& req, http::Response& resp) {
        auto me = current_user(req);
        if (!me) {
            resp.json_error(401, "请先登录");
            return;
        }

        auto body = req.json();
        if (!body) {
            resp.json_error(400, "请求正文不是合法 JSON");
            return;
        }

        std::string action = jstr(*body, "action");
        if (action.empty()) {
            resp.json_error(400, "缺少 action");
            return;
        }

        json payload = json::object();
        if (body->contains("instanceId")) {
            payload["instanceId"] = (*body)["instanceId"];
        }

        try {
            auto t = core::store().create(action, me->user_id, payload);

            json data = task_to_json(t);
            data["qr_content"] = core::TaskStore::qr_content(t);

            resp.json_ok(data);
        } catch (const std::exception& e) {
            LOG_WARN("[API] 创建任务失败: ", e.what());
            resp.json_error(400, e.what());
        }
    });

    srv.get("/api/task/:task_id/status",
            [](const http::Request& req, http::Response& resp) {
        auto it = req.path_params.find("task_id");
        if (it == req.path_params.end()) {
            resp.json_error(400, "缺少 task_id");
            return;
        }

        auto t = core::store().get(it->second);
        if (!t) {
            resp.json_error(404, "任务不存在");
            return;
        }
        resp.json_ok(task_to_json(*t));
    });

    srv.get("/api/task/list", [](const http::Request& req, http::Response& resp) {
        std::string status = req.get("status");

        auto tasks = core::store().list(status, 50);
        json arr = json::array();
        for (const auto& t : tasks) {
            arr.push_back(task_to_json(t));
        }
        resp.json_ok(arr);
    });

    srv.post("/api/qr/scan",
             [&queue](const http::Request& req, http::Response& resp) {
        auto body = req.json();
        if (!body) {
            resp.json_error(400, "请求正文不是合法 JSON");
            return;
        }

        std::string qr_content = jstr(*body, "qrContent");
        if (qr_content.empty()) {
            resp.json_error(400, "缺少 qrContent");
            return;
        }

        std::string task_id;
        try {
            auto j = json::parse(qr_content);
            task_id = jstr(j, "t");
        } catch (...) {
            if (qr_content.size() == 12) {
                task_id = qr_content;
            }
        }

        if (task_id.empty()) {
            resp.json_error(400, "二维码内容无法解析");
            return;
        }

        auto claimed = core::store().claim(task_id);
        if (!claimed) {
            resp.json_ok("任务已处理或已过期");
            return;
        }

        LOG_INFO("[API] 扫码触发任务 ", task_id, " 动作=", claimed->action);

        execute_task(*claimed, queue);

        auto final_task = core::store().get(task_id);
        if (final_task) {
            resp.json_ok(task_to_json(*final_task));
        } else {
            resp.json_ok("任务已执行");
        }
    });

    LOG_INFO("[路由] 任务路由已注册");
}

}
