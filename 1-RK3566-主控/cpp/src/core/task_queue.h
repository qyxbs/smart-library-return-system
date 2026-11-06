// =============================================================================
// task_queue.h —— 机械爪任务队列
// =============================================================================
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
    PRIO_BORROW   = 1,   // 用户借书 / 取书（最高）
    PRIO_RETRIEVE = 2,   // 管理员取书
    PRIO_SHELVE   = 3,   // 上架（批量任务，可以等）
};

struct Task {
    int priority = PRIO_SHELVE;
    int64_t seq = 0;              // 同优先级的排序依据（自增）
    std::string action;           // "borrow" / "retrieve" / "shelve"
    int coord_y = 0;              // 目标坐标（发给 STM32 的位置号）

    // ---- 执行结果（由 worker 填写）----
    bool finished = false;        // 是否已执行完
    bool ok = false;              // 成功与否
    std::string message;          // 结果描述
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
        int pending = 0;                           // 排队中几个
        std::string current_action;                // 正在做什么（空=空闲）
        int current_coord = 0;
        std::vector<std::pair<std::string,int>> queued;   // 排队明细 (动作, 坐标)
    };
    Status status();

private:
    // worker 线程的主函数
    void worker_loop();

    std::mutex mtx_;
    std::condition_variable cv_;        // worker 睡在这里等任务
    std::queue<TaskPtr> queue_;         // 【注意】这里用普通队列 + 手动排序

    bool running_ = false;
    int64_t seq_counter_ = 0;

    // 真正执行动作的函数（默认为空 → 用内置的模拟实现）
    Executor executor_;

    // 当前正在执行的任务（给 status 用）
    std::string current_action_;
    int current_coord_ = 0;
};

}  // namespace core
