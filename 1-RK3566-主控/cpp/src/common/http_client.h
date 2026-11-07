// http_client.h —— 简易 HTTP 客户端
#pragma once

#include <map>
#include <string>

namespace http {

struct ClientResponse {
    int status = 0;
    std::string body;
    std::map<std::string, std::string> headers;
    std::string error;

    bool ok() const { return status >= 200 && status < 300; }
};

ClientResponse request(const std::string& method,
                       const std::string& url,
                       const std::string& body = "",
                       const std::map<std::string, std::string>& headers = {},
                       int timeout_ms = 5000);

ClientResponse get(const std::string& url, int timeout_ms = 5000);
ClientResponse post_json(const std::string& url, const std::string& json_body,
                         int timeout_ms = 5000);

}
