// task_store.h
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
    std::string task_id;
    std::string action;
    int64_t user_id = 0;
    nlohmann::json payload;
    std::string status;
    nlohmann::json result;
    std::string created_at;
    std::string expires_at;
    std::string completed_at;
};

class TaskStore {
public:
    TaskStore() = default;

    TaskRecord create(const std::string& action, int64_t user_id,
                      const nlohmann::json& payload);

    std::optional<TaskRecord> get(const std::string& task_id);

    std::optional<TaskRecord> claim(const std::string& task_id);

    void complete(const std::string& task_id, const nlohmann::json& result);
    void fail(const std::string& task_id, const std::string& error);

    std::vector<TaskRecord> list(const std::string& status = "", int limit = 50);

    void start_cleanup();
    void stop_cleanup();

    void cleanup_once();

    static std::string qr_content(const TaskRecord& t);

    void init();

private:
    std::optional<TaskRecord> load_from_db(const std::string& task_id);
    void persist(const TaskRecord& t);

    std::mutex mtx_;
    std::map<std::string, TaskRecord> tasks_;

    bool cleanup_running_ = false;
};

TaskStore& store();

}
