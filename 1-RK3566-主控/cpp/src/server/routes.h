// routes.h —— 路由注册的声明
#pragma once

#include "common/http.h"
#include "core/task_queue.h"

namespace routes {

void register_book_routes(http::Server& srv, core::TaskQueue& queue);

void register_user_routes(http::Server& srv);

void register_task_routes(http::Server& srv, core::TaskQueue& queue);

void register_system_routes(http::Server& srv, core::TaskQueue& queue);

void register_k230_routes(http::Server& srv, core::TaskQueue& queue);

bool require_admin(const http::Request& req, http::Response& resp);

struct AuthUser {
    int64_t user_id = 0;
    std::string openid;
    int role = 0;
};
std::optional<AuthUser> current_user(const http::Request& req);

std::string make_token(const std::string& openid);

}
