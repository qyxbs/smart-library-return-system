// main_bridge.cpp — K230 桥接进程（:9998 / :9997 / :9999 / :9995）

#include <chrono>
#include <csignal>
#include <cstdlib>
#include <thread>

#include "bridge/bridge.h"
#include "common/config.h"
#include "common/log.h"
#include "data/db.h"

static volatile sig_atomic_t g_running = 1;

static void on_signal(int) {
    g_running = 0;
}

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;

    const char* log_path = std::getenv("BOOK_LOG_PATH");
    logger::init(logger::Level::INFO,
                 log_path ? log_path : "/userdata/log/k230_bridge_cpp.log");

    LOG_INFO("========================================");
    LOG_INFO("智能图书系统 · K230 桥接");
    LOG_INFO("========================================");

    try {
        db::init();
    } catch (const std::exception& e) {
        LOG_ERROR("数据库初始化失败: ", e.what());
        return 1;
    }

    std::signal(SIGINT, on_signal);
    std::signal(SIGTERM, on_signal);

    bridge::K230Bridge b;
    if (!b.start()) {
        LOG_ERROR("桥接启动失败（端口可能被占用）");
        return 1;
    }

    LOG_INFO("桥接就绪：");
    LOG_INFO("  K230#1 扫码  → 0.0.0.0:", cfg::K2301_PORT);
    LOG_INFO("  K230#2 通知  → 0.0.0.0:", cfg::K2302_NOTIFY_PORT);
    LOG_INFO("  K230#2 下发  → 127.0.0.1:", cfg::K2302_CMD_PORT);
    LOG_INFO("  本地命令     → 0.0.0.0:", cfg::BRIDGE_CMD_PORT);

    while (g_running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    LOG_INFO("收到退出信号，正在关闭...");
    b.stop();
    LOG_INFO("已退出");
    return 0;
}
