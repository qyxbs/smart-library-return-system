// =============================================================================
// bridge.h —— 进程 B：K230 桥接
// =============================================================================
#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace bridge {

class K230Bridge {
public:
    K230Bridge() = default;
    ~K230Bridge();

    K230Bridge(const K230Bridge&) = delete;
    K230Bridge& operator=(const K230Bridge&) = delete;

    // 启动所有监听（每个端口一个线程）
    bool start();

    // 停止（关闭所有监听 socket，线程退出）
    void stop();

    struct Status {
        bool k2301_connected = false;     // 扫码板连上了吗
        bool k2302_available = false;     // 导航板最近能连上吗
        int  books_scanned = 0;           // 累计扫到多少本
        std::string last_book;            // 最近一次扫到的书号
        std::string last_result;          // 最近一次处理结果
        std::string last_notify;          // 最近一次 K230#2 通知（TRIGGER/DONE/FETCH_DONE）
    };
    Status status();

private:
    // ---- 三个 TCP 服务器 ----
    void run_k2301_server();     // :9998 收扫码结果
    void run_notify_server();    // :9997 收 K230#2 通知
    void run_cmd_server();       // :9995 收本机命令

    // ---- 业务处理 ----
    // 处理一个扫到的书号：查库 → 拿坐标 → 转给 K230#2
    std::string handle_book_id(const std::string& book_id_str);

    // 给 K230 #2 发一行命令，等回复
    // msg 形如 "15,5"（Y,X 坐标）或 "START_RETURN"
    std::string send_to_k2302(const std::string& msg, int timeout_ms = 30000);

    // 坐标换算：DB 的 coord_y（11-28）→ AprilTag 的 (Y, X)
    //   15 → (1, 5)；23 → (2, 3)
    static bool coord_to_apriltag(int coord_y, int& y, int& x);

    // ---- 监听 socket 的创建/销毁 ----
    int create_listener(int port);
    void close_listener(int& fd);

    // ---- 状态 ----
    std::atomic<bool> running_{false};
    int fd_k2301_ = -1;
    int fd_notify_ = -1;
    int fd_cmd_ = -1;

    std::thread th_k2301_;
    std::thread th_notify_;
    std::thread th_cmd_;

    std::mutex mtx_;
    Status status_;
    int k2301_client_fd_ = -1;   // 当前连着的扫码板（用来回消息）
};

}  // namespace bridge
