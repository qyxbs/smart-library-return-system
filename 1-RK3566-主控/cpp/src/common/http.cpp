// =============================================================================
// http.cpp —— HTTP 服务器实现
// =============================================================================

#include "common/http.h"

#include <arpa/inet.h>    // inet_ntop —— 把二进制 IP 转成字符串
#include <netinet/in.h>   // sockaddr_in —— IPv4 地址结构
#include <sys/socket.h>   // socket / bind / listen / accept / recv / send / setsockopt
#include <unistd.h>       // close / read / write

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstring>        // strerror
#include <sstream>
#include <thread>         // std::thread

#include "common/log.h"

namespace http {

std::string url_decode(const std::string& s) {
    std::string out;
    out.reserve(s.size());

    for (size_t i = 0; i < s.size(); ++i) {
        char c = s[i];

        if (c == '+') {
            // 查询串里的 + 代表空格
            out += ' ';
        } else if (c == '%' && i + 2 < s.size()) {
            // %XX：把后面两位十六进制转成一个字节
            auto hex_val = [](char ch) -> int {
                if (ch >= '0' && ch <= '9') return ch - '0';
                if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
                if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
                return -1;
            };
            int hi = hex_val(s[i + 1]);
            int lo = hex_val(s[i + 2]);
            if (hi >= 0 && lo >= 0) {
                out += static_cast<char>((hi << 4) | lo);
                i += 2;   // 跳过已经处理的两个字符
            } else {
                out += c;  // 格式不对，原样保留
            }
        } else {
            out += c;
        }
    }
    return out;
}

std::map<std::string, std::string> parse_query(const std::string& query) {
    std::map<std::string, std::string> out;
    if (query.empty()) return out;

    size_t start = 0;
    while (start < query.size()) {
        // 找下一个 & 的位置
        size_t amp = query.find('&', start);
        std::string pair = (amp == std::string::npos)
                               ? query.substr(start)
                               : query.substr(start, amp - start);

        // 拆成 key=value
        size_t eq = pair.find('=');
        if (eq != std::string::npos) {
            std::string key = url_decode(pair.substr(0, eq));
            std::string val = url_decode(pair.substr(eq + 1));
            if (!key.empty()) {
                out[key] = val;   // 同名参数后者覆盖前者
            }
        } else if (!pair.empty()) {
            out[url_decode(pair)] = "";
        }

        if (amp == std::string::npos) break;
        start = amp + 1;
    }
    return out;
}

std::string Request::get(const std::string& key, const std::string& def) const {
    auto it = params.find(key);
    return it == params.end() ? def : it->second;
}

int64_t Request::path_int(const std::string& key, int64_t def) const {
    auto it = path_params.find(key);
    if (it == path_params.end()) return def;
    try {
        return std::stoll(it->second);
    } catch (...) {
        return def;   // 不是合法数字就用默认值
    }
}

std::optional<nlohmann::json> Request::json() const {
    if (body.empty()) {
        return std::nullopt;
    }
    try {
        return nlohmann::json::parse(body);
    } catch (const std::exception&) {
        // 正文不是合法 JSON
        return std::nullopt;
    }
}

void Response::json_ok(const nlohmann::json& data) {
    nlohmann::json j;
    j["code"] = 200;
    j["data"] = data;
    body = j.dump();       // dump() = 把 JSON 对象转成字符串
    content_type = "application/json; charset=utf-8";
    status = 200;
}

void Response::json_error(int code, const std::string& message) {
    nlohmann::json j;
    j["code"] = code;
    j["message"] = message;
    body = j.dump();
    content_type = "application/json; charset=utf-8";
    status = code;
}

void Response::text(const std::string& s, int code) {
    body = s;
    content_type = "text/plain; charset=utf-8";
    status = code;
}

void Response::html(const std::string& s, int code) {
    body = s;
    content_type = "text/html; charset=utf-8";
    status = code;
}

namespace {

// 把 "/api/book/add" 切成 {"api","book","add"}
std::vector<std::string> split_path(const std::string& path) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : path) {
        if (c == '/') {
            if (!cur.empty()) {
                out.push_back(cur);
                cur.clear();
            }
        } else {
            cur += c;
        }
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

}  // namespace

void Server::get(const std::string& path, Handler h) {
    routes_.push_back({"GET", split_path(path), std::move(h)});
}

void Server::post(const std::string& path, Handler h) {
    routes_.push_back({"POST", split_path(path), std::move(h)});
}

void Server::put(const std::string& path, Handler h) {
    routes_.push_back({"PUT", split_path(path), std::move(h)});
}

void Server::del(const std::string& path, Handler h) {
    routes_.push_back({"DELETE", split_path(path), std::move(h)});
}

Server::Handler* Server::find_handler(const std::string& method,
                                      const std::string& path,
                                      std::map<std::string, std::string>& out_params) {
    std::vector<std::string> req_segs = split_path(path);

    for (auto& r : routes_) {
        if (r.method != method) continue;
        if (r.segments.size() != req_segs.size()) continue;

        bool matched = true;
        std::map<std::string, std::string> tmp;

        for (size_t i = 0; i < r.segments.size(); ++i) {
            const std::string& pat = r.segments[i];
            const std::string& val = req_segs[i];

            if (!pat.empty() && pat[0] == ':') {
                // 参数段：名字是冒号后面的部分
                tmp[pat.substr(1)] = val;
            } else if (pat != val) {
                matched = false;
                break;
            }
        }

        if (matched) {
            out_params = std::move(tmp);
            return &r.handler;
        }
    }
    return nullptr;
}

Server::Server() = default;

Server::~Server() {
    stop();
    if (listen_fd_ >= 0) {
        ::close(listen_fd_);
        listen_fd_ = -1;
    }
}

bool Server::listen(int port, const std::string& bind_addr) {
    listen_fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd_ < 0) {
        LOG_ERROR("[HTTP] socket() 失败: ", std::strerror(errno));
        return false;
    }

    int opt = 1;
    ::setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(port));
    if (bind_addr == "0.0.0.0" || bind_addr.empty()) {
        addr.sin_addr.s_addr = INADDR_ANY;   // 所有网卡
    } else {
        ::inet_pton(AF_INET, bind_addr.c_str(), &addr.sin_addr);
    }

    if (::bind(listen_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        LOG_ERROR("[HTTP] bind() 失败（端口 ", port, "）: ", std::strerror(errno));
        ::close(listen_fd_);
        listen_fd_ = -1;
        return false;
    }

    if (::listen(listen_fd_, 128) < 0) {
        LOG_ERROR("[HTTP] listen() 失败: ", std::strerror(errno));
        ::close(listen_fd_);
        listen_fd_ = -1;
        return false;
    }

    port_ = port;
    LOG_INFO("[HTTP] 监听 0.0.0.0:", port, " 成功");
    return true;
}

void Server::run() {
    if (listen_fd_ < 0) {
        LOG_ERROR("[HTTP] 还没 listen 就调 run()");
        return;
    }

    LOG_INFO("[HTTP] 进入 accept 循环，等待连接");

    while (listen_fd_ >= 0) {
        sockaddr_in client_addr{};
        socklen_t addr_len = sizeof(client_addr);

        int client_fd = ::accept(listen_fd_,
                                 reinterpret_cast<sockaddr*>(&client_addr),
                                 &addr_len);
        if (client_fd < 0) {
            if (errno == EINTR) {
                continue;   // 被信号打断，重试
            }
            LOG_WARN("[HTTP] accept() 失败: ", std::strerror(errno));
            continue;
        }

        // 把客户端 IP 转成字符串（日志用）
        char ip_buf[INET_ADDRSTRLEN] = {0};
        ::inet_ntop(AF_INET, &client_addr.sin_addr, ip_buf, sizeof(ip_buf));

        std::string client_ip(ip_buf);
        std::thread(&Server::handle_client, this, client_fd, client_ip).detach();
    }
}

void Server::stop() {
    if (listen_fd_ >= 0) {
        ::shutdown(listen_fd_, SHUT_RDWR);
        ::close(listen_fd_);
        listen_fd_ = -1;
        LOG_INFO("[HTTP] 已停止监听");
    }
}

void Server::handle_client(int client_fd, const std::string& client_ip) {
    timeval tv{};
    tv.tv_sec = 5;
    tv.tv_usec = 0;
    ::setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    Request req;
    req.client_ip = client_ip;

    if (read_request(client_fd, req)) {
        req.params = parse_query(req.query);

        Response resp;

        Handler* h = find_handler(req.method, req.path, req.path_params);

        if (h != nullptr) {
            try {
                (*h)(req, resp);
            } catch (const std::exception& e) {
                // 处理函数抛异常 → 回 500，但不能让整个服务崩掉
                LOG_ERROR("[HTTP] 处理 ", req.method, " ", req.path,
                          " 时异常: ", e.what());
                resp.json_error(500, std::string("服务器内部错误: ") + e.what());
            }
        } else if (not_found_) {
            // 兜底处理（静态文件 / 404）
            try {
                not_found_(req, resp);
            } catch (const std::exception& e) {
                LOG_ERROR("[HTTP] 兜底处理异常: ", e.what());
                resp.json_error(500, "服务器内部错误");
            }
        } else {
            resp.json_error(404, "接口不存在: " + req.path);
        }

        write_response(client_fd, resp);

        LOG_INFO("[HTTP] ", client_ip, " ", req.method, " ", req.path,
                 " → ", resp.status);
    }

    ::close(client_fd);
}

bool Server::read_request(int fd, Request& req) {
    std::string buffer;
    char tmp[4096];
    size_t header_end = std::string::npos;

    while (true) {
        ssize_t n = ::recv(fd, tmp, sizeof(tmp), 0);
        if (n <= 0) {
            return false;   // 连接断开 / 超时 / 出错
        }
        buffer.append(tmp, static_cast<size_t>(n));

        // 找 "\r\n\r\n"
        header_end = buffer.find("\r\n\r\n");
        if (header_end != std::string::npos) {
            break;   // 头部读完了
        }

        // 防御：头部过长直接拒绝（防止恶意请求占内存）
        if (buffer.size() > 64 * 1024) {
            LOG_WARN("[HTTP] 请求头过大，已拒绝");
            return false;
        }
    }

    std::string head = buffer.substr(0, header_end);
    size_t first_line_end = head.find("\r\n");
    std::string request_line = head.substr(0, first_line_end);

    // 按空格切三段
    size_t sp1 = request_line.find(' ');
    if (sp1 == std::string::npos) {
        return false;
    }
    size_t sp2 = request_line.find(' ', sp1 + 1);
    if (sp2 == std::string::npos) {
        return false;
    }

    req.method = request_line.substr(0, sp1);
    std::string target = request_line.substr(sp1 + 1, sp2 - sp1 - 1);
    req.version = request_line.substr(sp2 + 1);

    // 把 "/api/book/search?keyword=三体" 拆成 path 和 query
    size_t qpos = target.find('?');
    if (qpos == std::string::npos) {
        req.path = target;
    } else {
        req.path = target.substr(0, qpos);
        req.query = target.substr(qpos + 1);
    }

    size_t pos = first_line_end + 2;   // 跳过请求行后面的 \r\n
    while (pos < head.size()) {
        size_t line_end = head.find("\r\n", pos);

        std::string line;
        if (line_end == std::string::npos) {
            line = head.substr(pos);   // 最后一行：取到结尾
            pos = head.size();
        } else {
            line = head.substr(pos, line_end - pos);
            pos = line_end + 2;
        }

        if (line.empty()) break;

        size_t colon = line.find(':');
        if (colon == std::string::npos) continue;

        std::string key = line.substr(0, colon);
        std::string val = line.substr(colon + 1);

        // 去掉值前面的空格
        while (!val.empty() && (val[0] == ' ' || val[0] == '\t')) {
            val.erase(val.begin());
        }

        // 头部键统一转小写（HTTP 头不区分大小写，转小写方便查找）
        std::transform(key.begin(), key.end(), key.begin(),
                       [](unsigned char c) { return std::tolower(c); });

        req.headers[key] = val;
    }

    size_t content_length = 0;
    auto it = req.headers.find("content-length");
    if (it != req.headers.end()) {
        try {
            content_length = static_cast<size_t>(std::stoul(it->second));
        } catch (...) {
            content_length = 0;
        }
    }

    // 已经读到的正文部分（可能和头部一起读进来了）
    std::string body = buffer.substr(header_end + 4);

    // 不够就继续读
    while (body.size() < content_length) {
        ssize_t n = ::recv(fd, tmp, sizeof(tmp), 0);
        if (n <= 0) {
            break;   // 连接断了，body 不完整（后面 JSON 解析会失败，返回 400）
        }
        body.append(tmp, static_cast<size_t>(n));
    }

    if (body.size() > content_length) {
        body.resize(content_length);   // 多读的截掉
    }
    req.body = body;

    return true;
}

void Server::write_response(int fd, const Response& resp) {
    const char* phrase = "OK";
    switch (resp.status) {
        case 200: phrase = "OK"; break;
        case 201: phrase = "Created"; break;
        case 400: phrase = "Bad Request"; break;
        case 401: phrase = "Unauthorized"; break;
        case 403: phrase = "Forbidden"; break;
        case 404: phrase = "Not Found"; break;
        case 500: phrase = "Internal Server Error"; break;
        case 503: phrase = "Service Unavailable"; break;
        default:  phrase = "OK"; break;
    }

    std::ostringstream oss;
    oss << "HTTP/1.1 " << resp.status << " " << phrase << "\r\n";
    oss << "Content-Type: " << resp.content_type << "\r\n";
    oss << "Content-Length: " << resp.body.size() << "\r\n";
    oss << "Connection: close\r\n";       // 本实现不支持 keep-alive
    oss << "Access-Control-Allow-Origin: *\r\n";   // 允许跨域（网页调试用）

    for (const auto& kv : resp.extra_headers) {
        oss << kv.first << ": " << kv.second << "\r\n";
    }

    oss << "\r\n";
    std::string head = oss.str();

    auto send_all = [fd](const char* data, size_t len) -> bool {
        size_t sent = 0;
        while (sent < len) {
            ssize_t n = ::send(fd, data + sent, len - sent, MSG_NOSIGNAL);
            if (n <= 0) {
                return false;
            }
            sent += static_cast<size_t>(n);
        }
        return true;
    };

    send_all(head.data(), head.size());
    if (!resp.body.empty()) {
        send_all(resp.body.data(), resp.body.size());
    }
}

}  // namespace http
