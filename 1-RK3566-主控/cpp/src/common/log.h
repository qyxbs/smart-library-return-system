// log.h —— 线程安全的日志工具
#pragma once

#include <sstream>
#include <string>
#include <utility>

namespace logger {

enum class Level {
    DEBUG = 0,
    INFO  = 1,
    WARN  = 2,
    ERROR = 3,
};

void init(Level min_level = Level::INFO, const std::string& to_file = "");

void write(Level level, const std::string& message);

template <typename... Args>
inline std::string fmt(Args&&... args) {
    std::ostringstream oss;
    (oss << ... << std::forward<Args>(args));
    return oss.str();
}

}

#define LOG_DEBUG(...) do { ::logger::write(::logger::Level::DEBUG, ::logger::fmt(__VA_ARGS__)); } while (0)
#define LOG_INFO(...)  do { ::logger::write(::logger::Level::INFO,  ::logger::fmt(__VA_ARGS__)); } while (0)
#define LOG_WARN(...)  do { ::logger::write(::logger::Level::WARN,  ::logger::fmt(__VA_ARGS__)); } while (0)
#define LOG_ERROR(...) do { ::logger::write(::logger::Level::ERROR, ::logger::fmt(__VA_ARGS__)); } while (0)
