// =============================================================================
// log.cpp —— 日志系统的实现
// =============================================================================

#include "common/log.h"

#include <chrono>    // std::chrono —— 取当前时间、计算毫秒
#include <cstdio>    // fprintf / stderr
#include <ctime>     // std::time_t / std::tm / localtime_r
#include <fstream>   // std::ofstream —— 写日志文件
#include <mutex>     // std::mutex / std::lock_guard

namespace logger {

namespace {

// 当前的最低输出级别。默认 INFO（DEBUG 不输出）。
Level g_min_level = Level::INFO;

// 日志文件流。为空表示不写文件。
std::ofstream g_log_file;

std::mutex g_write_mtx;

const char* level_name(Level lv) {
    switch (lv) {
        case Level::DEBUG: return "DEBUG";
        case Level::INFO:  return "INFO ";
        case Level::WARN:  return "WARN ";
        case Level::ERROR: return "ERROR";
    }
    return "?????";
}

std::string timestamp() {
    using namespace std::chrono;

    auto now = system_clock::now();
    std::time_t secs = system_clock::to_time_t(now);

    // 取毫秒部分：先算总毫秒数，再对 1000 取余
    auto ms = duration_cast<milliseconds>(now.time_since_epoch()).count() % 1000;

    // 转成本地时间的年月日时分秒
    std::tm tm_buf{};
    localtime_r(&secs, &tm_buf);   // _r 版本：线程安全，结果写进 tm_buf

    char buf[64];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d.%03ld",
                  tm_buf.tm_year + 1900,   // tm_year 是从 1900 年起的偏移量
                  tm_buf.tm_mon + 1,       // tm_mon 是 0-11，所以要 +1
                  tm_buf.tm_mday,
                  tm_buf.tm_hour,
                  tm_buf.tm_min,
                  tm_buf.tm_sec,
                  static_cast<long>(ms));
    return std::string(buf);
}

}  // namespace

void init(Level min_level, const std::string& to_file) {
    // 加锁：虽然 init 一般在启动时（还没起别的线程）调用，
    // 但加上锁可以防止"边跑边改日志配置"时出问题。
    std::lock_guard<std::mutex> lock(g_write_mtx);

    g_min_level = min_level;

    if (!to_file.empty()) {
        // std::ios::app = 追加模式（不覆盖已有内容）
        // 打开失败不影响程序运行——日志写不了总比程序崩了强
        g_log_file.open(to_file, std::ios::app);
        if (!g_log_file.is_open()) {
            std::fprintf(stderr, "[logger] 警告：无法打开日志文件 %s，日志只输出到终端\n",
                         to_file.c_str());
        }
    }
}

void write(Level level, const std::string& message) {
    if (level < g_min_level) {
        return;
    }

    // 拼出完整的一行：时间 + 级别 + 正文
    // 在锁外拼，减少锁的持有时间
    std::string line;
    line.reserve(64 + message.size());   // 预分配空间，避免多次 realloc
    line += timestamp();
    line += " [";
    line += level_name(level);
    line += "] ";
    line += message;
    line += '\n';

    // 加锁，保证"整行一次性写出去"，不会和别的线程交错
    std::lock_guard<std::mutex> lock(g_write_mtx);

    std::fwrite(line.data(), 1, line.size(), stderr);
    std::fflush(stderr);

    // 如果配了日志文件，同时写一份
    if (g_log_file.is_open()) {
        g_log_file << line;
        g_log_file.flush();   // 立即落盘——崩溃时也能保住最后几条日志
    }
}

}  // namespace logger
