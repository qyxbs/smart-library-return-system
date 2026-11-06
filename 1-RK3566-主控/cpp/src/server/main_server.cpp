// =============================================================================
// main_server.cpp —— 进程 A：HTTP 服务（book_server）
// =============================================================================

#include <csignal>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>

#include <nlohmann/json.hpp>

#include "common/config.h"
#include "common/http.h"
#include "common/http_client.h"
#include "common/log.h"
#include "core/printer.h"
#include "core/task_queue.h"
#include "core/task_store.h"
#include "core/uart.h"
#include "data/db.h"
#include "server/routes.h"

static volatile sig_atomic_t g_running = 1;
static http::Server* g_server = nullptr;

static void on_signal(int sig) {
    (void)sig;
    g_running = 0;
    if (g_server != nullptr) {
        g_server->stop();
    }
}

// 读整个文件（静态页面用）
static bool read_file_safe(const std::string& path, std::string& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f.is_open()) return false;
    std::ostringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}

static void handle_fallback(const http::Request& req, http::Response& resp) {

    const std::string voice_prefix = "/api/voice/";
    if (req.path.compare(0, voice_prefix.size(), voice_prefix) == 0) {
        std::string target = std::string(cfg::VOICE_SERVICE_URL) +
                             req.path.substr(voice_prefix.size() - 1);  // 保留开头的 /
        // 上面这句的效果：/api/voice/xxx → http://127.0.0.1:8890/xxx

        LOG_INFO("[代理] ", req.method, " ", req.path, " → ", target);

        std::map<std::string, std::string> headers;
        headers["Content-Type"] = req.headers.count("content-type")
                                      ? req.headers.at("content-type")
                                      : "application/json";

        auto r = http::request(req.method, target, req.body, headers, 30000);

        if (r.status == 0) {
            resp.json_error(502, "语音服务不可达: " + r.error);
            return;
        }

        resp.status = r.status;
        resp.body = r.body;
        resp.content_type = "application/json; charset=utf-8";
        return;
    }

    if (req.path == "/" || req.path == "/screen") {
        std::string static_dir = std::getenv("BOOK_STATIC_DIR")
                                     ? std::getenv("BOOK_STATIC_DIR")
                                     : cfg::STATIC_DIR_DEFAULT;
        std::string html;

        if (read_file_safe(static_dir + "/screen.html", html)) {
            resp.html(html);
            return;
        }

        // 本机开发时，静态目录可能不在这里，试试当前目录
        if (read_file_safe("static/screen.html", html)) {
            resp.html(html);
            return;
        }

        resp.json_error(404, "找不到 screen.html（请检查 BOOK_STATIC_DIR）");
        return;
    }

    const std::string static_prefix = "/static/";
    if (req.path.compare(0, static_prefix.size(), static_prefix) == 0) {
        std::string filename = req.path.substr(static_prefix.size());

        // 防路径穿越
        if (filename.empty() ||
            filename.find("..") != std::string::npos ||
            filename[0] == '/') {
            resp.json_error(403, "非法路径");
            return;
        }

        std::string static_dir = std::getenv("BOOK_STATIC_DIR")
                                     ? std::getenv("BOOK_STATIC_DIR")
                                     : cfg::STATIC_DIR_DEFAULT;
        std::string content;
        if (!read_file_safe(static_dir + "/" + filename, content)) {
            resp.json_error(404, "文件不存在: " + filename);
            return;
        }

        std::string ct = "application/octet-stream";
        auto ends_with = [&filename](const std::string& suffix) {
            return filename.size() >= suffix.size() &&
                   filename.compare(filename.size() - suffix.size(), suffix.size(), suffix) == 0;
        };
        if (ends_with(".js"))   ct = "application/javascript; charset=utf-8";
        else if (ends_with(".css")) ct = "text/css; charset=utf-8";
        else if (ends_with(".html")) ct = "text/html; charset=utf-8";
        else if (ends_with(".png")) ct = "image/png";
        else if (ends_with(".jpg") || ends_with(".jpeg")) ct = "image/jpeg";
        else if (ends_with(".svg")) ct = "image/svg+xml";
        else if (ends_with(".json")) ct = "application/json; charset=utf-8";

        resp.status = 200;
        resp.content_type = ct;
        resp.body = content;
        return;
    }

    resp.json_error(404, "接口不存在: " + req.path);
}

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;

    const char* log_path = std::getenv("BOOK_LOG_PATH");
    logger::init(logger::Level::INFO,
                 log_path ? log_path : "/userdata/log/book_server_cpp.log");

    LOG_INFO("========================================");
    LOG_INFO("智能图书系统 · HTTP 服务");
    LOG_INFO("========================================");

    try {
        db::init();
    } catch (const std::exception& e) {
        LOG_ERROR("数据库初始化失败: ", e.what());
        return 1;
    }

    bool uart_ready = core::init_uart();
    if (!uart_ready) {
        LOG_WARN("串口不可用，进入模拟模式（机械爪动作只打日志）");
    }

    bool printer_ready = core::init_printer();
    if (!printer_ready) {
        LOG_WARN("打印机不可用，进入模拟模式");
    }

    core::TaskQueue queue;

    queue.set_executor([](const std::string& action, int coord_y,
                          std::string& message) -> bool {
        if (action == "shelve") {
            bool ok = core::uart_store(coord_y);
            message = ok ? "上架指令已发送" : "上架指令发送失败";
            return ok;
        }
        if (action == "retrieve") {
            bool ok = core::uart_retrieve(coord_y);
            message = ok ? "取书指令已发送" : "取书指令发送失败";
            return ok;
        }
        if (action == "borrow") {
            // 借书 = 从书架上把这本取下来，和 retrieve 用同一个协议
            bool ok = core::uart_retrieve(coord_y);
            message = ok ? "借书指令已发送" : "借书指令发送失败";
            return ok;
        }
        message = "未知动作: " + action;
        return false;
    });

    queue.start();

    // 任务系统（二维码任务）
    core::store().init();
    core::store().start_cleanup();

    http::Server server;
    routes::register_book_routes(server, queue);
    routes::register_user_routes(server);
    routes::register_system_routes(server, queue);
    routes::register_k230_routes(server, queue);
    routes::register_task_routes(server, queue);

    server.set_not_found(handle_fallback);

    struct sigaction sa {};
    sa.sa_handler = on_signal;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, nullptr);    // Ctrl+C
    sigaction(SIGTERM, &sa, nullptr);   // kill 命令

    int port = cfg::HTTP_PORT;
    if (const char* env_port = std::getenv("BOOK_HTTP_PORT")) {
        try { port = std::stoi(env_port); } catch (...) { port = cfg::HTTP_PORT; }
    }

    if (!server.listen(port, "0.0.0.0")) {
        LOG_ERROR("HTTP 服务启动失败（端口 ", port, " 可能被占用）");
        queue.stop();
        return 1;
    }

    g_server = &server;

    LOG_INFO("========================================");
    LOG_INFO("服务就绪：http://0.0.0.0:", port);
    LOG_INFO("  健康检查: curl http://127.0.0.1:", port, "/api/health");
    LOG_INFO("  管理界面: http://0.0.0.0:", port, "/screen");
    LOG_INFO("  串口状态: ", uart_ready ? "真实设备" : "模拟模式");
    LOG_INFO("  打印状态: ", printer_ready ? "真实设备" : "模拟模式");
    LOG_INFO("  数据库  : ", db::current_path());
    LOG_INFO("========================================");

    // 阻塞在这里，直到 stop() 被调用
    // 【run() 返回后说明收到了退出信号】
    server.run();

    LOG_INFO("正在关闭...");
    core::store().stop_cleanup();
    queue.stop();
    g_server = nullptr;

    LOG_INFO("已退出");
    return 0;
}
