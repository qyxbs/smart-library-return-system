// =============================================================================
// routes.h —— 路由注册的声明
// =============================================================================
#pragma once

#include "common/http.h"
#include "core/task_queue.h"

namespace routes {

// 书目、实例、槽位相关
void register_book_routes(http::Server& srv, core::TaskQueue& queue);

// 用户、鉴权相关
void register_user_routes(http::Server& srv);

// 二维码任务、扫码回调
void register_task_routes(http::Server& srv, core::TaskQueue& queue);

// 健康检查、打印、日志、语音代理、静态页面
void register_system_routes(http::Server& srv, core::TaskQueue& queue);

// K230 相关、管理员上架/下架、输入法
void register_k230_routes(http::Server& srv, core::TaskQueue& queue);

bool require_admin(const http::Request& req, http::Response& resp);

// 取当前登录用户（没登录返回 nullopt）
struct AuthUser {
    int64_t user_id = 0;
    std::string openid;
    int role = 0;
};
std::optional<AuthUser> current_user(const http::Request& req);

// 生成带签名的登录 token
std::string make_token(const std::string& openid);

}  // namespace routes
