// =============================================================================
// config.h —— 全局配置常量
// =============================================================================
#pragma once

#include <cstdint>
#include <string>

namespace cfg {

constexpr int HTTP_PORT        = 9777;   // 本进程（book_server）对外的 HTTP 端口
                                          // 小程序 / HDMI 上的浏览器都连这个
constexpr int BRIDGE_CMD_PORT  = 9995;
                                          // 这是本机 127.0.0.1 上的 TCP，不对外暴露
constexpr int K2301_PORT       = 9998;   // K230 #1（扫码板）连进来的端口
                                          // 注意：是 K230 主动连我们，我们被动 accept
constexpr int K2302_CMD_PORT   = 9999;   // 我们主动连 K230 #2（导航板），给它下发坐标

// K230 #2 的地址（默认本机，部署时可设 K2302_HOST 环境变量）
inline std::string k2302_host() {
    const char* env = std::getenv("K2302_HOST");
    return env && *env ? std::string(env) : "127.0.0.1";
}
constexpr int K2302_NOTIFY_PORT = 9997;  // K230 #2 反向通知我们（TRIGGER / DONE）
                                          // 方向同样是 K230#2 主动连我们

// UDP 广播发现端口：K230 开机后不知道 RK3566 的 IP，靠这个广播包找到我们
constexpr int UDP_BEACON_PORT  = 19998;

constexpr const char* UART4_DEV = "/dev/ttyS4";   // 9600  —— 给 STM32 发坐标（FA/FB + 坐标字节）
constexpr const char* UART5_DEV = "/dev/ttyS5";   // 38400 —— 给 STM32 发控制字（FD 88 88 FD）
constexpr const char* UART9_DEV = "/dev/ttyS9";   // 115200 —— EM20L 热敏打印机

constexpr int UART4_BAUD = 9600;
constexpr int UART5_BAUD = 38400;
constexpr int UART9_BAUD = 115200;

constexpr const char* DB_PATH_DEFAULT = "/userdata/book_app/book.db";

constexpr int MAX_BORROW_COUNT   = 5;    // 一个用户最多同时借几本书
constexpr int BORROW_DAYS        = 30;   // 借阅期限（天），超期算逾期
constexpr double FINE_PER_DAY    = 0.5;  // 逾期罚款：每天 0.5 元

// 状态只能按业务规则流转：
//   0(待贴标) --录入--> 2(待上架) --上架--> 3(在架) --借出--> 1(已借出) --归还--> 2
constexpr int ST_PENDING_STICKER = 0;   // 待贴标：刚录入，二维码还没贴上
constexpr int ST_BORROWED        = 1;   // 已借出：被用户拿走了
constexpr int ST_PENDING_SHELF   = 2;   // 待上架：码贴好了/刚还回来，等着机械爪放回书架
constexpr int ST_ON_SHELF        = 3;   // 在架：在书架上，可以被借

// 用户角色
constexpr int ROLE_USER  = 0;   // 普通用户
constexpr int ROLE_ADMIN = 1;   // 管理员

constexpr const char* STATIC_DIR_DEFAULT = "/userdata/book_app/static";
                                          // screen.html 所在目录
constexpr const char* VOICE_SERVICE_URL  = "http://127.0.0.1:8890";
                                          // 本进程只做反向代理转发，不算任何 AI

}  // namespace cfg
