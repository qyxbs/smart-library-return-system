// task_store.cpp —— 二维码任务系统实现

#include "core/task_store.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <random>
#include <stdexcept>
#include <thread>

#include "common/config.h"
#include "common/log.h"
#include "data/db.h"

namespace core {

namespace {

constexpr int TASK_EXPIRE_SECONDS = 300;

constexpr int CLEANUP_INTERVAL_SECONDS = 60;

std::string make_task_id() {
    static std::mutex rng_mtx;
    static std::mt19937_64 rng{std::random_device{}()};

    std::lock_guard<std::mutex> lock(rng_mtx);
    uint64_t v = rng();

    char buf[16];
    std::snprintf(buf, sizeof(buf), "%012llx",
                  static_cast<unsigned long long>(v & 0xFFFFFFFFFFFFULL));
    return std::string(buf);
}

std::string time_plus_seconds(int seconds) {
    std::time_t t = std::time(nullptr) + seconds;
    std::tm tm_buf{};
    localtime_r(&t, &tm_buf);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm_buf);
    return std::string(buf);
}

}

void TaskStore::init() {
    LOG_INFO("[任务] 任务系统就绪");
}

TaskRecord TaskStore::create(const std::string& action, int64_t user_id,
                              const nlohmann::json& payload) {
    if (action != "borrow" && action != "return") {
        throw std::runtime_error("未知动作: " + action);
    }

    if (action == "borrow") {
        int64_t instance_id = 0;
        if (payload.contains("instanceId")) {
            if (payload["instanceId"].is_string()) {
                try { instance_id = std::stoll(payload["instanceId"].get<std::string>()); }
                catch (...) { instance_id = 0; }
            } else if (payload["instanceId"].is_number()) {
                instance_id = payload["instanceId"].get<int64_t>();
            }
        }
        if (instance_id == 0) {
            throw std::runtime_error("借书需要指定 instanceId");
        }

        auto inst = db::instance_get(instance_id);
        if (!inst) {
            throw std::runtime_error("书本实例不存在");
        }
        if (inst->status != cfg::ST_ON_SHELF) {
            throw std::runtime_error("《" + inst->title + "》不在架上，无法借阅");
        }
        if (db::user_is_frozen(user_id)) {
            throw std::runtime_error("账号已冻结，无法借阅");
        }
        if (db::user_get_fine(user_id) > 0) {
            throw std::runtime_error("有未缴罚款，请先缴纳后再借");
        }
        if (db::user_borrow_count(user_id) >= cfg::MAX_BORROW_COUNT) {
            throw std::runtime_error("已达借书上限（" +
                                     std::to_string(cfg::MAX_BORROW_COUNT) +
                                     "本），请先归还");
        }
        if (db::user_has_overdue(user_id)) {
            throw std::runtime_error("有逾期未还的书，请先归还后再借");
        }
    } else if (action == "return") {
        auto borrowed = db::instance_borrowed_list(user_id);
        if (borrowed.empty()) {
            throw std::runtime_error("没有借阅中的书");
        }
    }

    TaskRecord t;
    t.task_id     = make_task_id();
    t.action      = action;
    t.user_id     = user_id;
    t.payload     = payload;
    t.status      = "pending";
    t.result      = nlohmann::json::object();
    t.created_at  = db::now_str();
    t.expires_at  = time_plus_seconds(TASK_EXPIRE_SECONDS);

    {
        std::lock_guard<std::mutex> lock(mtx_);
        tasks_[t.task_id] = t;
    }
    persist(t);

    LOG_INFO("[任务] 创建 ", t.task_id, " 动作=", action, " 用户=", user_id,
             " 过期=", t.expires_at);
    return t;
}

std::optional<TaskRecord> TaskStore::get(const std::string& task_id) {
    TaskRecord t;

    {
        std::lock_guard<std::mutex> lock(mtx_);
        auto it = tasks_.find(task_id);
        if (it != tasks_.end()) {
            t = it->second;
        } else {
            auto loaded = load_from_db(task_id);
            if (!loaded) return std::nullopt;
            t = *loaded;
            tasks_[task_id] = t;
        }
    }

    if (t.status == "pending" && !t.expires_at.empty()) {
        if (db::now_str() > t.expires_at) {
            t.status = "expired";
            t.result = nlohmann::json{{"error", "task_expired"},
                                      {"message", "任务已过期，请重新生成"}};
            {
                std::lock_guard<std::mutex> lock(mtx_);
                tasks_[task_id] = t;
            }
            persist(t);
            LOG_INFO("[任务] ", task_id, " 已过期");
        }
    }

    return t;
}

std::optional<TaskRecord> TaskStore::claim(const std::string& task_id) {
    std::lock_guard<std::mutex> lock(mtx_);

    auto it = tasks_.find(task_id);
    if (it == tasks_.end()) {
        auto loaded = load_from_db(task_id);
        if (!loaded) return std::nullopt;
        tasks_[task_id] = *loaded;
        it = tasks_.find(task_id);
    }

    TaskRecord& t = it->second;

    if (t.status != "pending") {
        LOG_WARN("[任务] ", task_id, " 认领失败，当前状态=", t.status);
        return std::nullopt;
    }

    // 过期的也不能认领
    if (!t.expires_at.empty() && db::now_str() > t.expires_at) {
        t.status = "expired";
        persist(t);
        LOG_WARN("[任务] ", task_id, " 已过期，拒绝认领");
        return std::nullopt;
    }

    t.status = "processing";
    persist(t);

    LOG_INFO("[任务] ", task_id, " 已认领");
    return t;
}

void TaskStore::complete(const std::string& task_id, const nlohmann::json& result) {
    std::lock_guard<std::mutex> lock(mtx_);
    auto it = tasks_.find(task_id);
    if (it == tasks_.end()) return;

    it->second.status = "done";
    it->second.result = result;
    it->second.completed_at = db::now_str();
    persist(it->second);

    LOG_INFO("[任务] ", task_id, " 完成");
}

void TaskStore::fail(const std::string& task_id, const std::string& error) {
    std::lock_guard<std::mutex> lock(mtx_);
    auto it = tasks_.find(task_id);
    if (it == tasks_.end()) return;

    it->second.status = "failed";
    it->second.result = nlohmann::json{{"error", error}};
    it->second.completed_at = db::now_str();
    persist(it->second);

    LOG_WARN("[任务] ", task_id, " 失败: ", error);
}

std::vector<TaskRecord> TaskStore::list(const std::string& status, int limit) {
    std::vector<TaskRecord> out;

    {
        std::lock_guard<std::mutex> lock(mtx_);
        for (auto& kv : tasks_) {
            if (status.empty() || kv.second.status == status) {
                out.push_back(kv.second);
            }
        }
    }

    std::sort(out.begin(), out.end(),
              [](const TaskRecord& a, const TaskRecord& b) {
                  return a.created_at > b.created_at;
              });

    if (static_cast<int>(out.size()) > limit) {
        out.resize(limit);
    }
    return out;
}

void TaskStore::cleanup_once() {
    std::lock_guard<std::mutex> lock(mtx_);

    std::string now = db::now_str();
    int expired_count = 0;
    int removed_count = 0;

    for (auto it = tasks_.begin(); it != tasks_.end();) {
        TaskRecord& t = it->second;

        if (t.status == "pending" && !t.expires_at.empty() && now > t.expires_at) {
            t.status = "expired";
            persist(t);
            ++expired_count;
        }

        bool removable = (t.status == "done" || t.status == "failed" ||
                          t.status == "expired");
        if (removable) {
            if (now > time_plus_seconds(-900)) {
            }
            if (!t.completed_at.empty()) {
                std::string cutoff = time_plus_seconds(-600);
                if (now > cutoff && t.completed_at < cutoff) {
                    it = tasks_.erase(it);
                    ++removed_count;
                    continue;
                }
            }
        }
        ++it;
    }

    if (expired_count > 0 || removed_count > 0) {
        LOG_INFO("[任务] 清理：过期 ", expired_count, " 个，移除 ", removed_count, " 个");
    }
}

void TaskStore::start_cleanup() {
    if (cleanup_running_) return;
    cleanup_running_ = true;

    std::thread([this]() {
        LOG_INFO("[任务] 清理线程已启动（每 ", CLEANUP_INTERVAL_SECONDS, " 秒）");
        while (cleanup_running_) {
            std::this_thread::sleep_for(std::chrono::seconds(CLEANUP_INTERVAL_SECONDS));
            if (!cleanup_running_) break;
            cleanup_once();
        }
    }).detach();
}

void TaskStore::stop_cleanup() {
    cleanup_running_ = false;
}

void TaskStore::persist(const TaskRecord& t) {
    db::TaskRow row;
    row.task_id      = t.task_id;
    row.action       = t.action;
    row.user_id      = t.user_id;
    row.payload      = t.payload.dump();
    row.status       = t.status;
    row.result       = t.result.dump();
    row.created_at   = t.created_at;
    row.expires_at   = t.expires_at;
    row.completed_at = t.completed_at;

    try {
        db::task_save(row);
    } catch (const std::exception& e) {
        LOG_WARN("[任务] 持久化失败: ", e.what());
    }
}

std::optional<TaskRecord> TaskStore::load_from_db(const std::string& task_id) {
    auto row = db::task_load(task_id);
    if (!row) return std::nullopt;

    TaskRecord t;
    t.task_id     = row->task_id;
    t.action      = row->action;
    t.user_id     = row->user_id;
    t.status      = row->status;
    t.created_at  = row->created_at;
    t.expires_at  = row->expires_at;
    t.completed_at = row->completed_at;

    try { t.payload = nlohmann::json::parse(row->payload); }
    catch (...) { t.payload = nlohmann::json::object(); }

    try { t.result = nlohmann::json::parse(row->result); }
    catch (...) { t.result = nlohmann::json::object(); }

    return t;
}

std::string TaskStore::qr_content(const TaskRecord& t) {
    nlohmann::json j;
    j["t"]  = t.task_id;
    j["a"]  = t.action;
    j["ts"] = std::time(nullptr);
    return j.dump();
}

TaskStore& store() {
    static TaskStore instance;
    return instance;
}

}
