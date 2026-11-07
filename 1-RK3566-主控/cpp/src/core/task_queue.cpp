// task_queue.cpp —— 任务队列的实现

#include "core/task_queue.h"

#include <algorithm>
#include <chrono>
#include <thread>

#include "common/log.h"

namespace core {

TaskQueue::TaskQueue() = default;

TaskQueue::~TaskQueue() {
    stop();
}

void TaskQueue::start() {
    std::lock_guard<std::mutex> lock(mtx_);
    if (running_) {
        return;
    }
    running_ = true;

    std::thread(&TaskQueue::worker_loop, this).detach();

    LOG_INFO("[队列] 任务队列已启动（单 worker 线程）");
}

void TaskQueue::stop() {
    {
        std::lock_guard<std::mutex> lock(mtx_);
        if (!running_) {
            return;
        }
        running_ = false;
    }
    cv_.notify_all();
    LOG_INFO("[队列] 任务队列已停止");
}

TaskPtr TaskQueue::enqueue(int priority, const std::string& action, int coord_y,
                           int timeout_ms) {
    auto task = std::make_shared<Task>();
    task->priority = priority;
    task->action = action;
    task->coord_y = coord_y;

    std::unique_lock<std::mutex> lock(mtx_);

    task->seq = ++seq_counter_;
    queue_.push(task);

    LOG_INFO("[队列] 入队: ", action, " 坐标=", coord_y,
             " 优先级=", priority, " 当前排队=", queue_.size());

    cv_.notify_one();

    bool done = cv_.wait_for(lock, std::chrono::milliseconds(timeout_ms),
                             [&task]() { return task->finished; });

    if (!done) {
        task->ok = false;
        task->message = "机械爪响应超时";
        LOG_ERROR("[队列] 任务超时: ", action, " 坐标=", coord_y);
    }

    return task;
}

void TaskQueue::worker_loop() {
    LOG_INFO("[队列] worker 线程已就绪，等待任务");

    while (true) {
        TaskPtr task;

        {
            std::unique_lock<std::mutex> lock(mtx_);

            cv_.wait(lock, [this]() { return !queue_.empty() || !running_; });

            if (!running_) {
                LOG_INFO("[队列] worker 收到停止信号，退出");
                return;
            }

            auto best = queue_.front();
            size_t best_idx = 0;

            std::vector<TaskPtr> tmp;
            std::queue<TaskPtr> copy = queue_;
            while (!copy.empty()) {
                tmp.push_back(copy.front());
                copy.pop();
            }

            for (size_t i = 0; i < tmp.size(); ++i) {
                if (tmp[i]->priority < best->priority ||
                    (tmp[i]->priority == best->priority && tmp[i]->seq < best->seq)) {
                    best = tmp[i];
                    best_idx = i;
                }
            }

            std::queue<TaskPtr> newq;
            for (size_t i = 0; i < tmp.size(); ++i) {
                if (i != best_idx) newq.push(tmp[i]);
            }
            queue_ = newq;

            task = best;
            current_action_ = task->action;
            current_coord_ = task->coord_y;
        }

        LOG_INFO("[队列] 执行: ", task->action, " 坐标=", task->coord_y);

        bool ok = true;
        std::string message;

        Executor ex;
        {
            std::lock_guard<std::mutex> lock(mtx_);
            ex = executor_;
        }

        if (ex) {
            ok = ex(task->action, task->coord_y, message);
        } else {
            if (task->action == "shelve") {
                message = "上架指令已发送";
            } else if (task->action == "retrieve") {
                message = "取书指令已发送";
            } else if (task->action == "borrow") {
                message = "借书指令已发送";
            } else {
                ok = false;
                message = "未知动作: " + task->action;
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }

        {
            std::lock_guard<std::mutex> lock(mtx_);
            task->ok = ok;
            task->message = message;
            task->finished = true;

            current_action_.clear();
            current_coord_ = 0;
        }

        cv_.notify_all();

        LOG_INFO("[队列] 完成: ", task->action, " → ", message);
    }
}

void TaskQueue::set_executor(Executor ex) {
    std::lock_guard<std::mutex> lock(mtx_);
    executor_ = std::move(ex);
}

TaskQueue::Status TaskQueue::status() {
    std::lock_guard<std::mutex> lock(mtx_);

    Status st;
    st.pending = static_cast<int>(queue_.size());
    st.current_action = current_action_;
    st.current_coord = current_coord_;

    std::queue<TaskPtr> copy = queue_;
    while (!copy.empty()) {
        auto t = copy.front();
        copy.pop();
        st.queued.emplace_back(t->action, t->coord_y);
    }

    return st;
}

}
