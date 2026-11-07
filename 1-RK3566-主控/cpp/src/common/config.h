// config.h —— 全局配置常量
#pragma once

#include <cstdint>
#include <string>

namespace cfg {

constexpr int HTTP_PORT        = 9777;
constexpr int BRIDGE_CMD_PORT  = 9995;
constexpr int K2301_PORT       = 9998;
constexpr int K2302_CMD_PORT   = 9999;

inline std::string k2302_host() {
    const char* env = std::getenv("K2302_HOST");
    return env && *env ? std::string(env) : "127.0.0.1";
}
constexpr int K2302_NOTIFY_PORT = 9997;

constexpr int UDP_BEACON_PORT  = 19998;

constexpr const char* UART4_DEV = "/dev/ttyS4";   // 9600  —— 给 STM32 发坐标（FA/FB + 坐标字节）
constexpr const char* UART5_DEV = "/dev/ttyS5";
constexpr const char* UART9_DEV = "/dev/ttyS9";

constexpr int UART4_BAUD = 9600;
constexpr int UART5_BAUD = 38400;
constexpr int UART9_BAUD = 115200;

constexpr const char* DB_PATH_DEFAULT = "/userdata/book_app/book.db";

constexpr int MAX_BORROW_COUNT   = 5;
constexpr int BORROW_DAYS        = 30;
constexpr double FINE_PER_DAY    = 0.5;

constexpr int ST_PENDING_STICKER = 0;
constexpr int ST_BORROWED        = 1;
constexpr int ST_PENDING_SHELF   = 2;
constexpr int ST_ON_SHELF        = 3;

constexpr int ROLE_USER  = 0;
constexpr int ROLE_ADMIN = 1;

constexpr const char* STATIC_DIR_DEFAULT = "/userdata/book_app/static";
constexpr const char* VOICE_SERVICE_URL  = "http://127.0.0.1:8890";

}
