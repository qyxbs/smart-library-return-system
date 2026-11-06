// =============================================================================
// test_task_queue.cpp —— 任务队列测试
// =============================================================================
// 【测什么】
//   1. 基本功能：入队 → 执行 → 拿到结果
//   2. 优先级：高优先级插队（这是队列存在的意义）
//   3. 状态查询：能查到排队情况和当前任务
//
// 【怎么测优先级】
//   难点：enqueue() 是阻塞的——它等任务做完才返回。
//   如果串行调用 enqueue，根本不会有两个任务同时排队，就测不出先后。
//
//   办法：先丢一个"慢任务"占住 worker，趁它忙的时候并发提交几个不同优先级的任务，
//   这些任务就会在队列里堆积，worker 做完第一个后按优先级取。
// =============================================================================

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

    // =========================================================================
    // 记录执行顺序的执行器
    // =========================================================================
    // 【注意：这个 lambda 会在 worker 线程里被调用】
    //   所以往共享的 exec_order 里写必须加锁，否则数据竞争。
    std::mutex log_mtx;
    std::vector<std::string> exec_order;
    std::atomic<int> exec_count{0};

    core::TaskQueue queue;
    queue.set_executor([&](const std::string& action, int coord, std::string& msg) {
        // 模拟机械爪动作耗时（让它慢一点，方便观察排队）
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

    // =========================================================================
    // 1. 基本功能
    // =========================================================================
    {
        exec_order.clear();
        auto t = queue.enqueue(core::PRIO_SHELVE, "shelve", 15);
        check(t->finished, "任务执行完（finished=true）");
        check(t->ok, "任务返回成功");
        check(t->message == "执行完成", "能拿到执行器的返回消息");
        check(exec_order.size() == 1, "执行器被调用了一次");
        check(!exec_order.empty() && exec_order[0] == "shelve:15", "动作和坐标正确");
    }

    // =========================================================================
    // 2. 优先级：高优先级插队
    // =========================================================================
    // 场景：
    //   t=0   提交一个慢任务（占住 worker 约 80ms）
    //   t=5   同时提交三个任务（线程并发入队，顺序不确定）
    //           · 上架   坐标 30（最低优先级）
    //           · 借书   坐标 10（最高优先级）
    //           · 取书   坐标 20（中间）
    //   期望执行顺序：占位任务 → 借书(10) → 取书(20) → 上架(30)
    //   注意：三个任务的**提交顺序**可能乱，但**执行顺序**必须按优先级。
    // =========================================================================
    {
        exec_order.clear();

        // 占位任务：让 worker 忙起来
        std::thread blocker([&]() {
            queue.enqueue(core::PRIO_SHELVE, "shelve", 99);
        });
        std::this_thread::sleep_for(std::chrono::milliseconds(15));   // 确保占位任务已被取走

        // 三个不同优先级的任务并发提交
        std::thread t1([&]() { queue.enqueue(core::PRIO_SHELVE,   "shelve",   30); });
        std::thread t2([&]() { queue.enqueue(core::PRIO_BORROW,   "borrow",   10); });
        std::thread t3([&]() { queue.enqueue(core::PRIO_RETRIEVE, "retrieve", 20); });

        t1.join(); t2.join(); t3.join();
        blocker.join();

        std::printf("  实际执行顺序：");
        for (const auto& s : exec_order) std::printf("%s  ", s.c_str());
        std::printf("\n");

        check(exec_order.size() == 4, "一共执行了 4 个任务");

        // 第 1 个是占位任务
        check(exec_order.size() > 0 && exec_order[0] == "shelve:99", "第一个是占位任务");

        // 后面三个必须按优先级：borrow(10) → retrieve(20) → shelve(30)
        if (exec_order.size() == 4) {
            check(exec_order[1] == "borrow:10",   "第 2 个是借书（最高优先级）");
            check(exec_order[2] == "retrieve:20", "第 3 个是取书");
            check(exec_order[3] == "shelve:30",   "第 4 个是上架（最低优先级）");
        }
    }

    // =========================================================================
    // 3. 状态查询
    // =========================================================================
    {
        // 空闲时
        auto st = queue.status();
        check(st.pending == 0, "空闲时排队数为 0");

        // 忙的时候查（占位任务执行中）
        std::thread busy([&]() { queue.enqueue(core::PRIO_SHELVE, "shelve", 88); });
        std::this_thread::sleep_for(std::chrono::milliseconds(20));

        auto st2 = queue.status();
        check(st2.current_action == "shelve", "能查到当前正在执行的动作");
        check(st2.current_coord == 88, "能查到当前坐标");

        busy.join();
    }

    // =========================================================================
    // 4. 并发压力：20 个请求同时入队
    // =========================================================================
    // 【测什么】
    //   多线程同时入队时，队列不能丢任务、不能重复执行。
    // =========================================================================
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
