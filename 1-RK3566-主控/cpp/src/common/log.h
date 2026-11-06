// =============================================================================
// log.h —— 线程安全的日志工具
// =============================================================================
#pragma once

#include <sstream>   // std::ostringstream —— 拼字符串用（像 std::cout，但结果能取出来）
#include <string>
#include <utility>   // std::forward —— 完美转发参数用

namespace logger {

enum class Level {
    DEBUG = 0,   // 调试细节（正常运行时看不到，排查问题才开）
    INFO  = 1,   // 普通信息（程序启动、请求处理完成）
    WARN  = 2,   // 警告（能继续跑，但有不正常的地方）
    ERROR = 3,   // 错误（这次操作失败了）
};

void init(Level min_level = Level::INFO, const std::string& to_file = "");

// 输出一条日志。一般不要直接调它，用下面的 LOG_xxx 宏。
// 内部会加锁，保证多线程下每行日志完整不交错。
void write(Level level, const std::string& message);

template <typename... Args>
inline std::string fmt(Args&&... args) {
    std::ostringstream oss;
    (oss << ... << std::forward<Args>(args));   // C++17 折叠表达式：逐个 << 进流
    return oss.str();
}

}  // namespace logger

#define LOG_DEBUG(...) do { ::logger::write(::logger::Level::DEBUG, ::logger::fmt(__VA_ARGS__)); } while (0)
#define LOG_INFO(...)  do { ::logger::write(::logger::Level::INFO,  ::logger::fmt(__VA_ARGS__)); } while (0)
#define LOG_WARN(...)  do { ::logger::write(::logger::Level::WARN,  ::logger::fmt(__VA_ARGS__)); } while (0)
#define LOG_ERROR(...) do { ::logger::write(::logger::Level::ERROR, ::logger::fmt(__VA_ARGS__)); } while (0)
