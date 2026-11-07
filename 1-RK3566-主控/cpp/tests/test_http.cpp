// =============================================================================
// test_http.cpp —— HTTP 服务器测试
// =============================================================================
// 【测法】
//   1. 在本机启一个服务器（监听 18080）
//   2. 写一个极简的 HTTP 客户端，发各种请求
//   3. 检查响应对不对
//
// 【为什么用自带客户端而不是调 curl】
//   · 不依赖外部命令（板子上可能没 curl）
//   · 顺便演示"客户端怎么发 HTTP 请求"——这也是 socket 编程
// =============================================================================

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <thread>

#include "common/http.h"
#include "common/log.h"

// =============================================================================
// 极简 HTTP 客户端
// =============================================================================
// 发一个请求，把服务器回的原始字节整个读回来。
// 不解析响应（测试里用字符串查找判断），保持简单。
static std::string http_request(int port, const std::string& raw_request) {
    // 1. 建 socket
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return "";

    // 2. 连服务器
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(port));
    ::inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

    if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        ::close(fd);
        return "";
    }

    // 3. 发请求
    ::send(fd, raw_request.data(), raw_request.size(), MSG_NOSIGNAL);

    // 4. 一直读，直到服务器关闭连接（因为我们发的是 Connection: close）
    std::string resp;
    char buf[4096];
    ssize_t n;
    while ((n = ::recv(fd, buf, sizeof(buf), 0)) > 0) {
        resp.append(buf, static_cast<size_t>(n));
    }

    ::close(fd);
    return resp;
}

// 从完整响应里取出正文（\r\n\r\n 之后的部分）
static std::string body_of(const std::string& resp) {
    size_t p = resp.find("\r\n\r\n");
    return p == std::string::npos ? "" : resp.substr(p + 4);
}

static int g_pass = 0;
static int g_fail = 0;

static void check(bool cond, const char* msg) {
    if (cond) {
        ++g_pass;
    } else {
        ++g_fail;
        std::printf("  [FAIL] %s\n", msg);
    }
}

int main() {
    logger::init(logger::Level::WARN);   // 测试时只看警告以上，输出简洁

    const int TEST_PORT = 18080;

    // =========================================================================
    // 准备：注册几个测试路由
    // =========================================================================
    http::Server srv;

    // 最简单的 GET
    srv.get("/ping", [](const http::Request&, http::Response& r) {
        r.json_ok({{"pong", true}});
    });

    // 带路径参数
    srv.get("/api/item/:id", [](const http::Request& req, http::Response& r) {
        r.json_ok({{"id", req.path_int("id")}});
    });

    // 带查询参数（测试中文解码）
    srv.get("/api/search", [](const http::Request& req, http::Response& r) {
        r.json_ok({{"keyword", req.get("keyword")}, {"page", req.get("page", "1")}});
    });

    // POST + JSON 正文
    srv.post("/api/echo", [](const http::Request& req, http::Response& r) {
        auto j = req.json();
        if (!j) {
            r.json_error(400, "正文不是合法 JSON");
            return;
        }
        r.json_ok({{"received", *j}});
    });

    // 缺参数的 POST（测试错误路径）
    srv.post("/api/need_id", [](const http::Request& req, http::Response& r) {
        auto j = req.json();
        if (!j || !j->contains("id")) {
            r.json_error(400, "缺少 id 参数");
            return;
        }
        r.json_ok({{"ok", true}});
    });

    // =========================================================================
    // 启动服务器（另起线程跑阻塞循环）
    // =========================================================================
    if (!srv.listen(TEST_PORT, "127.0.0.1")) {
        std::printf("无法监听端口 %d，测试中止\n", TEST_PORT);
        return 1;
    }

    std::thread server_thread([&srv]() { srv.run(); });

    // 给服务器一点时间进入 accept 状态
    // 【为什么需要等】
    //   listen() 返回后，run() 才会调 accept()。
    //   如果马上发请求，可能服务器还没开始 accept——虽然内核会把连接排队，
    //   但为了测试稳定，稍等一下。
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    std::printf("=== HTTP 服务器测试 ===\n");

    // =========================================================================
    // 1. 基本 GET
    // =========================================================================
    {
        std::string resp = http_request(TEST_PORT,
            "GET /ping HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n");
        check(resp.find("200 OK") != std::string::npos, "GET /ping 返回 200");
        std::string body = body_of(resp);
        check(body.find("\"pong\"") != std::string::npos, "响应含 pong 字段");
        check(body.find("\"code\":200") != std::string::npos, "统一包了 code 字段");
    }

    // =========================================================================
    // 2. 路径参数
    // =========================================================================
    {
        std::string resp = http_request(TEST_PORT,
            "GET /api/item/123 HTTP/1.1\r\nHost: localhost\r\n\r\n");
        std::string body = body_of(resp);
        check(body.find("\"id\":123") != std::string::npos, "路径参数 :id 解析正确");
    }

    // =========================================================================
    // 3. 查询参数 + 中文 URL 解码
    // =========================================================================
    {
        // "三体" 的 UTF-8 是 E4 B8 89 E4 BD 93，URL 编码为 %E4%B8%89%E4%BD%93
        std::string resp = http_request(TEST_PORT,
            "GET /api/search?keyword=%E4%B8%89%E4%BD%93&page=2 HTTP/1.1\r\n"
            "Host: localhost\r\n\r\n");
        std::string body = body_of(resp);
        check(body.find("三体") != std::string::npos, "中文 URL 解码正确");
        check(body.find("\"page\":\"2\"") != std::string::npos, "第二个查询参数正确");
    }

    // =========================================================================
    // 4. POST + JSON 正文
    // =========================================================================
    {
        std::string payload = R"({"title":"三体","category":"文学"})";
        std::string req =
            "POST /api/echo HTTP/1.1\r\n"
            "Host: localhost\r\n"
            "Content-Type: application/json\r\n"
            "Content-Length: " + std::to_string(payload.size()) + "\r\n"
            "\r\n" + payload;

        std::string resp = http_request(TEST_PORT, req);
        std::string body = body_of(resp);
        check(resp.find("200 OK") != std::string::npos, "POST 返回 200");
        check(body.find("三体") != std::string::npos, "正文里的中文被正确接收");
    }

    // =========================================================================
    // 5. POST 缺参数 → 400
    // =========================================================================
    {
        std::string payload = R"({"foo":"bar"})";
        std::string req =
            "POST /api/need_id HTTP/1.1\r\n"
            "Host: localhost\r\n"
            "Content-Length: " + std::to_string(payload.size()) + "\r\n"
            "\r\n" + payload;

        std::string resp = http_request(TEST_PORT, req);
        check(resp.find("400") != std::string::npos, "缺参数返回 400");
        check(body_of(resp).find("缺少 id") != std::string::npos, "错误信息正确");
    }

    // =========================================================================
    // 6. 不存在的路径 → 404
    // =========================================================================
    {
        std::string resp = http_request(TEST_PORT,
            "GET /not/exist HTTP/1.1\r\nHost: localhost\r\n\r\n");
        check(resp.find("404") != std::string::npos, "未知路径返回 404");
    }

    // =========================================================================
    // 7. 方法不匹配 → 404（GET 去访问只有 POST 的路径）
    // =========================================================================
    {
        std::string resp = http_request(TEST_PORT,
            "GET /api/echo HTTP/1.1\r\nHost: localhost\r\n\r\n");
        check(resp.find("404") != std::string::npos, "方法不匹配返回 404");
    }

    // =========================================================================
    // 收尾
    // =========================================================================
    srv.stop();
    server_thread.join();

    std::printf("\n========================================\n");
    std::printf("通过 %d 项，失败 %d 项\n", g_pass, g_fail);
    std::printf("========================================\n");

    return g_fail == 0 ? 0 : 1;
}
