// =============================================================================
// bridge.cpp —— K230 桥接实现
// =============================================================================

#include "bridge/bridge.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <sstream>

#include "common/config.h"
#include "common/log.h"
#include "data/db.h"

namespace bridge {

namespace {

// 从 socket 读一行（以 \n 结尾）
// 返回 false 表示连接断了
bool read_line(int fd, std::string& line) {
    line.clear();
    char c;
    while (true) {
        ssize_t n = ::recv(fd, &c, 1, 0);
        if (n <= 0) return false;         // 断开或出错
        if (c == '\n') return true;       // 读完整一行
        if (c != '\r') line += c;         // 忽略 \r（兼容 \r\n）
        if (line.size() > 4096) return false;  // 防御：异常长的行
    }
}

}  // namespace

K230Bridge::~K230Bridge() {
    stop();
}

int K230Bridge::create_listener(int port) {
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        LOG_ERROR("[桥接] socket() 失败: ", std::strerror(errno));
        return -1;
    }

    // 允许立刻重新绑定（避免重启时 TIME_WAIT 占用端口）
    int opt = 1;
    ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(port));
    addr.sin_addr.s_addr = INADDR_ANY;

    if (::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        LOG_ERROR("[桥接] bind(", port, ") 失败: ", std::strerror(errno));
        ::close(fd);
        return -1;
    }

    if (::listen(fd, 8) < 0) {
        LOG_ERROR("[桥接] listen(", port, ") 失败: ", std::strerror(errno));
        ::close(fd);
        return -1;
    }

    return fd;
}

void K230Bridge::close_listener(int& fd) {
    if (fd >= 0) {
        ::shutdown(fd, SHUT_RDWR);
        ::close(fd);
        fd = -1;
    }
}

bool K230Bridge::start() {
    running_ = true;

    fd_k2301_ = create_listener(cfg::K2301_PORT);
    if (fd_k2301_ < 0) {
        LOG_ERROR("[桥接] 无法监听 K230#1 端口 ", cfg::K2301_PORT);
        running_ = false;
        return false;
    }

    fd_notify_ = create_listener(cfg::K2302_NOTIFY_PORT);
    if (fd_notify_ < 0) {
        LOG_ERROR("[桥接] 无法监听通知端口 ", cfg::K2302_NOTIFY_PORT);
        stop();
        return false;
    }

    fd_cmd_ = create_listener(cfg::BRIDGE_CMD_PORT);
    if (fd_cmd_ < 0) {
        LOG_ERROR("[桥接] 无法监听本地命令端口 ", cfg::BRIDGE_CMD_PORT);
        stop();
        return false;
    }

    th_k2301_ = std::thread(&K230Bridge::run_k2301_server, this);
    th_notify_ = std::thread(&K230Bridge::run_notify_server, this);
    th_cmd_ = std::thread(&K230Bridge::run_cmd_server, this);

    LOG_INFO("[桥接] 已启动：K230#1=:", cfg::K2301_PORT,
             " 通知=:", cfg::K2302_NOTIFY_PORT,
             " 本地命令=:", cfg::BRIDGE_CMD_PORT);
    return true;
}

void K230Bridge::stop() {
    if (!running_.exchange(false)) {
        return;
    }

    close_listener(fd_k2301_);
    close_listener(fd_notify_);
    close_listener(fd_cmd_);

    if (th_k2301_.joinable()) th_k2301_.join();
    if (th_notify_.joinable()) th_notify_.join();
    if (th_cmd_.joinable()) th_cmd_.join();

    LOG_INFO("[桥接] 已停止");
}

void K230Bridge::run_k2301_server() {
    LOG_INFO("[桥接] K230#1 监听线程就绪");

    while (running_) {
        sockaddr_in client{};
        socklen_t len = sizeof(client);

        int cfd = ::accept(fd_k2301_, reinterpret_cast<sockaddr*>(&client), &len);
        if (cfd < 0) {
            if (!running_) break;
            continue;
        }

        char ip[INET_ADDRSTRLEN] = {0};
        ::inet_ntop(AF_INET, &client.sin_addr, ip, sizeof(ip));
        LOG_INFO("[桥接] K230#1 已连接: ", ip);

        {
            std::lock_guard<std::mutex> lock(mtx_);
            status_.k2301_connected = true;
            k2301_client_fd_ = cfd;
        }

        // 长连接：一直读，直到断开
        std::string line;
        while (running_ && read_line(cfd, line)) {
            if (line.empty()) continue;

            LOG_INFO("[桥接] K230#1 扫到书号: ", line);
            std::string reply = handle_book_id(line);

            std::string out = reply + "\n";
            ::send(cfd, out.data(), out.size(), MSG_NOSIGNAL);

            std::lock_guard<std::mutex> lock(mtx_);
            status_.last_book = line;
            status_.last_result = reply;
            status_.books_scanned++;
        }

        {
            std::lock_guard<std::mutex> lock(mtx_);
            status_.k2301_connected = false;
            k2301_client_fd_ = -1;
        }
        ::close(cfd);
        LOG_INFO("[桥接] K230#1 断开");
    }
}

void K230Bridge::run_notify_server() {
    LOG_INFO("[桥接] 通知监听线程就绪");

    while (running_) {
        int cfd = ::accept(fd_notify_, nullptr, nullptr);
        if (cfd < 0) {
            if (!running_) break;
            continue;
        }

        std::string line;
        if (read_line(cfd, line)) {
            LOG_INFO("[桥接] K230#2 通知: ", line);
        }
        ::close(cfd);
    }
}

void K230Bridge::run_cmd_server() {
    LOG_INFO("[桥接] 本地命令线程就绪（:", cfg::BRIDGE_CMD_PORT, "）");

    while (running_) {
        int cfd = ::accept(fd_cmd_, nullptr, nullptr);
        if (cfd < 0) {
            if (!running_) break;
            continue;
        }

        std::string line;
        if (!read_line(cfd, line)) {
            ::close(cfd);
            continue;
        }

        std::string reply;

        if (line == "PING") {
            reply = "PONG";
        } else if (line.compare(0, 10, "ADMIN:WAKE") == 0) {
            std::lock_guard<std::mutex> lock(mtx_);
            if (k2301_client_fd_ < 0) {
                reply = "ERROR:k2301_not_connected";
            } else {
                const char* msg = "WAKE\n";
                ::send(k2301_client_fd_, msg, std::strlen(msg), MSG_NOSIGNAL);
                reply = "ACK";
                LOG_INFO("[桥接] 已向 K230#1 发送 WAKE");
            }
        } else if (line.compare(0, 11, "ADMIN:SLEEP") == 0) {
            std::lock_guard<std::mutex> lock(mtx_);
            if (k2301_client_fd_ < 0) {
                reply = "ERROR:k2301_not_connected";
            } else {
                const char* msg = "SLEEP\n";
                ::send(k2301_client_fd_, msg, std::strlen(msg), MSG_NOSIGNAL);
                reply = "ACK";
                LOG_INFO("[桥接] 已向 K230#1 发送 SLEEP");
            }
        } else if (line.compare(0, 11, "CHECK_SHELF") == 0) {
            // 查取书区有没有书：K230#2 转发给 STM32，回来 FAFA/FBFB
            std::string r = send_to_k2302("CHECK_SHELF", 8000);
            if (r.empty()) {
                reply = "ERROR:k2302_unreachable";
            } else if (r.find("FAFA") != std::string::npos ||
                       r.find("FA FA") != std::string::npos) {
                reply = "FAFA";   // 有书
            } else {
                reply = "FBFB";   // 空
            }
        } else if (line.compare(0, 12, "ADMIN_SHELVE") == 0 &&
                   line.find("STATUS") != std::string::npos) {
            reply = "{\"state\":\"idle\",\"message\":\"批量上架状态机未实现\"}";
        } else if (line.compare(0, 12, "ADMIN_SHELVE") == 0) {
            reply = "OK";
            LOG_INFO("[桥接] 收到批量上架请求（状态机简化，仅回 OK）");
        } else if (line == "STATUS") {
            auto st = status();
            std::ostringstream oss;
            oss << "k2301=" << (st.k2301_connected ? 1 : 0)
                << " scanned=" << st.books_scanned
                << " last=" << st.last_book;
            reply = oss.str();
        } else if (line.compare(0, 6, "FETCH:") == 0) {
            std::string id_str = line.substr(6);
            try {
                int64_t inst_id = std::stoll(id_str);
                auto inst = db::instance_get(inst_id);
                if (!inst) {
                    reply = "ERROR:not_found";
                } else if (inst->slot_id == 0) {
                    reply = "ERROR:not_on_shelf";
                } else {
                    int y = 0, x = 0;
                    if (!coord_to_apriltag(inst->coord_y, y, x)) {
                        reply = "ERROR:bad_coord";
                    } else {
                        std::string nav = send_to_k2302(
                            std::to_string(y) + "," + std::to_string(x));
                        reply = nav.empty() ? "ERROR:nav_failed" : "OK:" + nav;
                    }
                }
            } catch (...) {
                reply = "ERROR:bad_id";
            }
        } else {
            reply = "ERROR:unknown_command";
        }

        std::string out = reply + "\n";
        ::send(cfd, out.data(), out.size(), MSG_NOSIGNAL);
        ::close(cfd);
    }
}

std::string K230Bridge::handle_book_id(const std::string& book_id_str) {
    // 解析书号（QR 里存的是 book_id）
    int64_t book_id = 0;
    try {
        book_id = std::stoll(book_id_str);
    } catch (...) {
        return "ERROR:invalid_id";
    }

    // 查这本书"在架"的那一本实体
    auto inst = db::instance_get_on_shelf_by_book_id(book_id);
    if (!inst) {
        // 也可能二维码里存的是 instance_id
        inst = db::instance_get(book_id);
    }

    if (!inst) {
        return "ERROR:book_not_found";
    }
    if (inst->status != cfg::ST_ON_SHELF) {
        return "ERROR:not_on_shelf";
    }

    int y = 0, x = 0;
    if (!coord_to_apriltag(inst->coord_y, y, x)) {
        return "ERROR:bad_coord";
    }

    LOG_INFO("[桥接] 书《", inst->title, "》坐标 ", inst->coord_y,
             " → AprilTag (", y, ",", x, ")");

    std::string nav = send_to_k2302(std::to_string(y) + "," + std::to_string(x));
    if (nav.empty()) {
        return "ERROR:nav_failed";
    }

    return "OK";
}

std::string K230Bridge::send_to_k2302(const std::string& msg, int timeout_ms) {
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        LOG_ERROR("[桥接] 连接 K230#2 失败：socket 创建错误");
        return "";
    }

    // 连接超时
    timeval tv{};
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    ::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(cfg::K2302_CMD_PORT));
    ::inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

    if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        LOG_WARN("[桥接] 连不上 K230#2（", std::strerror(errno), "）");
        ::close(fd);
        return "";
    }

    {
        std::lock_guard<std::mutex> lock(mtx_);
        status_.k2302_available = true;
    }

    std::string out = msg + "\n";
    ::send(fd, out.data(), out.size(), MSG_NOSIGNAL);

    std::string reply;
    read_line(fd, reply);
    ::close(fd);

    LOG_INFO("[桥接] K230#2 回复: ", reply);
    return reply;
}

bool K230Bridge::coord_to_apriltag(int coord_y, int& y, int& x) {
    if (coord_y < 11 || coord_y > 28) {
        return false;
    }
    y = coord_y / 10;    // 整除取层号：15/10 = 1
    x = coord_y % 10;    // 取余取位置：15%10 = 5

    if (y < 1 || y > 2 || x < 1 || x > 8) {
        return false;
    }
    return true;
}

K230Bridge::Status K230Bridge::status() {
    std::lock_guard<std::mutex> lock(mtx_);
    return status_;
}

}  // namespace bridge
