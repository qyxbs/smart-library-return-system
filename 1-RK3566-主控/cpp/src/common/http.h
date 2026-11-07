// http.h —— HTTP 服务器
#pragma once

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace http {

struct Request {
    std::string method;
    std::string path;
    std::string query;
    std::string version;

    std::map<std::string, std::string> headers;
    std::string body;                            // 正文原始字节

    std::map<std::string, std::string> params;

    std::map<std::string, std::string> path_params;

    std::string client_ip;

    std::string get(const std::string& key, const std::string& def = "") const;

    int64_t path_int(const std::string& key, int64_t def = 0) const;

    std::optional<nlohmann::json> json() const;
};

struct Response {
    int status = 200;
    std::string content_type = "application/json; charset=utf-8";
    std::string body;
    std::map<std::string, std::string> extra_headers;

    void json_ok(const nlohmann::json& data);
    void json_error(int code, const std::string& message);

    void text(const std::string& s, int code = 200);
    void html(const std::string& s, int code = 200);
};

class Server {
public:
    using Handler = std::function<void(const Request& req, Response& resp)>;

    Server();
    ~Server();

    // 禁止拷贝（内部持有 socket 文件描述符和线程资源）
    Server(const Server&) = delete;
    Server& operator=(const Server&) = delete;

    void get(const std::string& path, Handler h);
    void post(const std::string& path, Handler h);
    void put(const std::string& path, Handler h);
    void del(const std::string& path, Handler h);

    void set_not_found(Handler h) { not_found_ = std::move(h); }

    bool listen(int port, const std::string& bind_addr = "0.0.0.0");

    void run();

    void stop();

    int port() const { return port_; }

private:
    struct Route {
        std::string method;
        std::vector<std::string> segments;
        Handler handler;
    };

    void handle_client(int client_fd, const std::string& client_ip);

    bool read_request(int fd, Request& req);

    void write_response(int fd, const Response& resp);

    Handler* find_handler(const std::string& method,
                          const std::string& path,
                          std::map<std::string, std::string>& out_params);

    int listen_fd_ = -1;
    int port_ = 0;
    std::vector<Route> routes_;
    Handler not_found_;
};

std::string url_decode(const std::string& s);

std::map<std::string, std::string> parse_query(const std::string& query);

}
