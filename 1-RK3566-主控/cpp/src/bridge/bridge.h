// bridge.h —— 进程 B：K230 桥接
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

    bool start();

    void stop();

    struct Status {
        bool k2301_connected = false;
        bool k2302_available = false;
        int  books_scanned = 0;
        std::string last_book;
        std::string last_result;
        std::string last_notify;
    };
    Status status();

private:
    void run_k2301_server();
    void run_notify_server();
    void run_cmd_server();

    std::string handle_book_id(const std::string& book_id_str);

    std::string send_to_k2302(const std::string& msg, int timeout_ms = 30000);

    static bool coord_to_apriltag(int coord_y, int& y, int& x);

    int create_listener(int port);
    void close_listener(int& fd);

    std::atomic<bool> running_{false};
    int fd_k2301_ = -1;
    int fd_notify_ = -1;
    int fd_cmd_ = -1;

    std::thread th_k2301_;
    std::thread th_notify_;
    std::thread th_cmd_;

    std::mutex mtx_;
    Status status_;
    int k2301_client_fd_ = -1;
};

}
