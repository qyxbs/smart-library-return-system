// test_task_queue.cpp —— 任务队列测试

#include <atomic>
#include <chrono>
#include <cstdio>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "common/log.h"
#include "core/task_queue.h"

static int g_pass = 0;
static int g_fail = 0;

static void check(bool cond, const std::string& msg) {
    if (cond) {
        ++g_pass;
    } else {
        ++g_fail;
        std::printf("  [FAIL] %s\n", msg.c_str());
    }
}

int main() {
    logger::init(logger::Level::WARN);

    //   所以往共享的 exec_order 里写必须加锁，否则数据竞争。
    std::mutex log_mtx;
    std::vector<std::string> exec_order;
    std::atomic<int> exec_count{0};

    core::TaskQueue queue;
    queue.set_executor([&](const std::string& action, int coord, std::string& msg) {
        std::this_thread::sleep_for(std::chrono::milliseconds(80));

        {
            std::lock_guard<std::mutex> lock(log_mtx);
            exec_order.push_back(action + ":" + std::to_string(coord));
        }
        ++exec_count;
        msg = "执行完成";
        return true;
    });
    queue.start();

    std::printf("=== 任务队列测试 ===\n");

    {
        exec_order.clear();
        auto t = queue.enqueue(core::PRIO_SHELVE, "shelve", 15);
        check(t->finished, "任务执行完（finished=true）");
        check(t->ok, "任务返回成功");
        check(t->message == "执行完成", "能拿到执行器的返回消息");
        check(exec_order.size() == 1, "执行器被调用了一次");
        check(!exec_order.empty() && exec_order[0] == "shelve:15", "动作和坐标正确");
    }

    //   注意：三个任务的**提交顺序**可能乱，但**执行顺序**必须按优先级。
    {
        exec_order.clear();

        std::thread blocker([&]() {
            queue.enqueue(core::PRIO_SHELVE, "shelve", 99);
        });
        std::this_thread::sleep_for(std::chrono::milliseconds(15));

        std::thread t1([&]() { queue.enqueue(core::PRIO_SHELVE,   "shelve",   30); });
        std::thread t2([&]() { queue.enqueue(core::PRIO_BORROW,   "borrow",   10); });
        std::thread t3([&]() { queue.enqueue(core::PRIO_RETRIEVE, "retrieve", 20); });

        t1.join(); t2.join(); t3.join();
        blocker.join();

        std::printf("  实际执行顺序：");
        for (const auto& s : exec_order) std::printf("%s  ", s.c_str());
        std::printf("\n");

        check(exec_order.size() == 4, "一共执行了 4 个任务");

        check(exec_order.size() > 0 && exec_order[0] == "shelve:99", "第一个是占位任务");

        // 后面三个必须按优先级：borrow(10) → retrieve(20) → shelve(30)
        if (exec_order.size() == 4) {
            check(exec_order[1] == "borrow:10",   "第 2 个是借书（最高优先级）");
            check(exec_order[2] == "retrieve:20", "第 3 个是取书");
            check(exec_order[3] == "shelve:30",   "第 4 个是上架（最低优先级）");
        }
    }

    {
        auto st = queue.status();
        check(st.pending == 0, "空闲时排队数为 0");

        std::thread busy([&]() { queue.enqueue(core::PRIO_SHELVE, "shelve", 88); });
        std::this_thread::sleep_for(std::chrono::milliseconds(20));

        auto st2 = queue.status();
        check(st2.current_action == "shelve", "能查到当前正在执行的动作");
        check(st2.current_coord == 88, "能查到当前坐标");

        busy.join();
    }

    //   多线程同时入队时，队列不能丢任务、不能重复执行。
    {
        exec_order.clear();
        exec_count = 0;

        std::vector<std::thread> threads;
        for (int i = 0; i < 20; ++i) {
            threads.emplace_back([&queue, i]() {
                int prio = (i % 3 == 0) ? core::PRIO_BORROW
                         : (i % 3 == 1) ? core::PRIO_RETRIEVE
                                        : core::PRIO_SHELVE;
                queue.enqueue(prio, "shelve", i);
            });
        }
        for (auto& t : threads) t.join();

        check(exec_count.load() == 20, "20 个任务全部被执行（不丢不重）");
        check(exec_order.size() == 20, "执行记录 20 条");
    }

    queue.stop();

    std::printf("\n========================================\n");
    std::printf("通过 %d 项，失败 %d 项\n", g_pass, g_fail);
    std::printf("========================================\n");

    return g_fail == 0 ? 0 : 1;
}
