// log.cpp —— 日志系统的实现

#include "common/log.h"

#include <chrono>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <mutex>

namespace logger {

namespace {

Level g_min_level = Level::INFO;

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

    auto ms = duration_cast<milliseconds>(now.time_since_epoch()).count() % 1000;

    std::tm tm_buf{};
    localtime_r(&secs, &tm_buf);

    char buf[64];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d.%03ld",
                  tm_buf.tm_year + 1900,
                  tm_buf.tm_mon + 1,
                  tm_buf.tm_mday,
                  tm_buf.tm_hour,
                  tm_buf.tm_min,
                  tm_buf.tm_sec,
                  static_cast<long>(ms));
    return std::string(buf);
}

}

void init(Level min_level, const std::string& to_file) {
    std::lock_guard<std::mutex> lock(g_write_mtx);

    g_min_level = min_level;

    if (!to_file.empty()) {
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

    std::string line;
    line.reserve(64 + message.size());
    line += timestamp();
    line += " [";
    line += level_name(level);
    line += "] ";
    line += message;
    line += '\n';

    std::lock_guard<std::mutex> lock(g_write_mtx);

    std::fwrite(line.data(), 1, line.size(), stderr);
    std::fflush(stderr);

    if (g_log_file.is_open()) {
        g_log_file << line;
        g_log_file.flush();
    }
}

}
