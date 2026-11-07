// =============================================================================
// http.h —— HTTP 服务器
// =============================================================================
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
    std::string method;    // "GET" / "POST" / "PUT" / "DELETE"
    std::string path;      // "/api/book/add"（不含查询串）
    std::string query;     // "keyword=三体&page=1"（? 后面的部分）
    std::string version;   // "HTTP/1.1"

    std::map<std::string, std::string> headers;  // 头部，键统一转成小写
    std::string body;                            // 正文原始字节

    // 查询参数（从 query 解析出来）
    // 例：?keyword=三体&page=1 → {{"keyword","三体"},{"page","1"}}
    std::map<std::string, std::string> params;

    std::map<std::string, std::string> path_params;

    // 客户端地址（日志用）
    std::string client_ip;

    // 取某个查询参数，没有就返回默认值
    std::string get(const std::string& key, const std::string& def = "") const;

    // 取路径参数并转成整数
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

    // 回纯文本 / HTML
    void text(const std::string& s, int code = 200);
    void html(const std::string& s, int code = 200);
};

class Server {
public:
    // 处理函数类型：收到请求 → 往 resp 里填内容
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

    // 注册一个"兜底"处理函数（所有路由都不匹配时调用）
    // 用来返回静态文件（screen.html）或 404
    void set_not_found(Handler h) { not_found_ = std::move(h); }

    bool listen(int port, const std::string& bind_addr = "0.0.0.0");

    // 进入主循环（阻塞，直到 stop() 被调用）
    void run();

    // 请求停止（可以从另一个线程调用）
    void stop();

    // 当前监听端口（listen 后有效）
    int port() const { return port_; }

private:
    struct Route {
        std::string method;
        std::vector<std::string> segments;   // 路径按 / 切开
        Handler handler;
    };

    // 处理一个客户端连接（每个连接一个线程）
    void handle_client(int client_fd, const std::string& client_ip);

    // 从 socket 读一个完整的 HTTP 请求
    // 返回 false 表示连接关闭或出错
    bool read_request(int fd, Request& req);

    // 把响应写回 socket
    void write_response(int fd, const Response& resp);

    // 按方法+路径找处理函数
    Handler* find_handler(const std::string& method,
                          const std::string& path,
                          std::map<std::string, std::string>& out_params);

    int listen_fd_ = -1;
    int port_ = 0;
    std::vector<Route> routes_;
    Handler not_found_;
};

std::string url_decode(const std::string& s);

// 解析查询串 "a=1&b=2" → {{"a","1"},{"b","2"}}
std::map<std::string, std::string> parse_query(const std::string& query);

}  // namespace http
