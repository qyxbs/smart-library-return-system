// =============================================================================
// =============================================================================
// 【这套系统解决什么问题】
#pragma once

#include <chrono>
#include <cstdint>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace core {

struct TaskRecord {
    std::string task_id;          // 12 位短 ID（二维码里放这个）
    std::string action;           // "borrow" / "return"
    int64_t user_id = 0;
    nlohmann::json payload;       // 附加数据，如 {"instanceId": 123}
    std::string status;           // pending / processing / done / failed / expired
    nlohmann::json result;        // 执行结果
    std::string created_at;
    std::string expires_at;
    std::string completed_at;
};

class TaskStore {
public:
    TaskStore() = default;

    TaskRecord create(const std::string& action, int64_t user_id,
                      const nlohmann::json& payload);

    // 查任务（会自动检查过期）
    std::optional<TaskRecord> get(const std::string& task_id);

    std::optional<TaskRecord> claim(const std::string& task_id);

    // 标记完成 / 失败
    void complete(const std::string& task_id, const nlohmann::json& result);
    void fail(const std::string& task_id, const std::string& error);

    // 列出最近的任务（监控页面用）
    std::vector<TaskRecord> list(const std::string& status = "", int limit = 50);

    void start_cleanup();
    void stop_cleanup();

    // 手动清理一次（测试用）
    void cleanup_once();

    // 二维码内容（小程序据此生成二维码）
    // 格式：{"t":"<task_id>","a":"<action>","ts":<创建时间戳>}
    static std::string qr_content(const TaskRecord& t);

    // 初始化（建 task 表 —— 数据库层已经建好了，这里做兼容检查）
    void init();

private:
    // 从数据库读
    std::optional<TaskRecord> load_from_db(const std::string& task_id);
    // 写数据库
    void persist(const TaskRecord& t);

    std::mutex mtx_;
    std::map<std::string, TaskRecord> tasks_;   // 内存缓存

    bool cleanup_running_ = false;
};

TaskStore& store();

}  // namespace core
