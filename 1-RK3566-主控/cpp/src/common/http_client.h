// =============================================================================
// http_client.h —— 简易 HTTP 客户端
// =============================================================================
#pragma once

#include <map>
#include <string>

namespace http {

struct ClientResponse {
    int status = 0;                       // HTTP 状态码（0 表示请求没发出去）
    std::string body;                     // 响应正文
    std::map<std::string, std::string> headers;  // 响应头（键小写）
    std::string error;                    // 出错时的描述

    bool ok() const { return status >= 200 && status < 300; }
};

ClientResponse request(const std::string& method,
                       const std::string& url,
                       const std::string& body = "",
                       const std::map<std::string, std::string>& headers = {},
                       int timeout_ms = 5000);

// 便捷函数
ClientResponse get(const std::string& url, int timeout_ms = 5000);
ClientResponse post_json(const std::string& url, const std::string& json_body,
                         int timeout_ms = 5000);

}  // namespace http
