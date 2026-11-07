// task_queue.h —— 机械爪任务队列
#pragma once

#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <vector>

namespace core {

enum TaskPriority {
    PRIO_BORROW   = 1,
    PRIO_RETRIEVE = 2,
    PRIO_SHELVE   = 3,
};

struct Task {
    int priority = PRIO_SHELVE;
    int64_t seq = 0;
    std::string action;
    int coord_y = 0;

    bool finished = false;
    bool ok = false;
    std::string message;
};

using TaskPtr = std::shared_ptr<Task>;

class TaskQueue {
public:
    TaskQueue();
    ~TaskQueue();

    TaskQueue(const TaskQueue&) = delete;
    TaskQueue& operator=(const TaskQueue&) = delete;

    void start();
    void stop();

    using Executor = std::function<bool(const std::string& action, int coord_y,
                                        std::string& message)>;
    void set_executor(Executor ex);

    TaskPtr enqueue(int priority, const std::string& action, int coord_y,
                    int timeout_ms = 30000);

    struct Status {
        int pending = 0;
        std::string current_action;
        int current_coord = 0;
        std::vector<std::pair<std::string,int>> queued;
    };
    Status status();

private:
    void worker_loop();

    std::mutex mtx_;
    std::condition_variable cv_;
    std::queue<TaskPtr> queue_;

    bool running_ = false;
    int64_t seq_counter_ = 0;

    Executor executor_;

    std::string current_action_;
    int current_coord_ = 0;
};

}
