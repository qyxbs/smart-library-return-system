// http_client.cpp —— HTTP 客户端实现

#include "common/http_client.h"

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstring>
#include <sstream>

#include "common/log.h"

namespace http {

namespace {

bool parse_url(const std::string& url, std::string& host, int& port,
               std::string& path) {
    // 必须 http:// 开头
    const std::string prefix = "http://";
    if (url.compare(0, prefix.size(), prefix) != 0) {
        return false;
    }

    std::string rest = url.substr(prefix.size());

    size_t slash = rest.find('/');
    std::string hostport = (slash == std::string::npos) ? rest : rest.substr(0, slash);
    path = (slash == std::string::npos) ? "/" : rest.substr(slash);

    size_t colon = hostport.find(':');
    if (colon == std::string::npos) {
        host = hostport;
        port = 80;
    } else {
        host = hostport.substr(0, colon);
        try {
            port = std::stoi(hostport.substr(colon + 1));
        } catch (...) {
            return false;
        }
    }

    return !host.empty();
}

}

ClientResponse request(const std::string& method,
                       const std::string& url,
                       const std::string& body,
                       const std::map<std::string, std::string>& headers,
                       int timeout_ms) {
    ClientResponse resp;

    std::string host, path;
    int port = 80;
    if (!parse_url(url, host, port, path)) {
        resp.error = "URL 格式不支持（只支持 http://）: " + url;
        return resp;
    }

    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    addrinfo* result = nullptr;
    int rc = ::getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &result);
    if (rc != 0 || result == nullptr) {
        resp.error = "域名解析失败: " + host;
        return resp;
    }

    int fd = ::socket(result->ai_family, result->ai_socktype, result->ai_protocol);
    if (fd < 0) {
        ::freeaddrinfo(result);
        resp.error = "socket 创建失败";
        return resp;
    }

    // 设置连接超时（否则连一个不存在的服务可能卡几十秒）
    timeval tv{};
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    ::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    if (::connect(fd, result->ai_addr, result->ai_addrlen) < 0) {
        ::close(fd);
        ::freeaddrinfo(result);
        resp.error = "连接失败: " + host + ":" + std::to_string(port) +
                     " (" + std::strerror(errno) + ")";
        return resp;
    }
    ::freeaddrinfo(result);

    std::ostringstream req;
    req << method << " " << path << " HTTP/1.1\r\n";
    req << "Host: " << host << ":" << port << "\r\n";
    req << "Connection: close\r\n";
    req << "Content-Length: " << body.size() << "\r\n";

    for (const auto& kv : headers) {
        req << kv.first << ": " << kv.second << "\r\n";
    }
    req << "\r\n";
    req << body;

    std::string raw = req.str();

    size_t sent = 0;
    while (sent < raw.size()) {
        ssize_t n = ::send(fd, raw.data() + sent, raw.size() - sent, MSG_NOSIGNAL);
        if (n <= 0) {
            ::close(fd);
            resp.error = "发送失败";
            return resp;
        }
        sent += static_cast<size_t>(n);
    }

    std::string data;
    char buf[4096];
    ssize_t n;
    while ((n = ::recv(fd, buf, sizeof(buf), 0)) > 0) {
        data.append(buf, static_cast<size_t>(n));
    }
    ::close(fd);

    size_t header_end = data.find("\r\n\r\n");
    if (header_end == std::string::npos) {
        resp.error = "响应格式错误（找不到头部结束标记）";
        return resp;
    }

    std::string head = data.substr(0, header_end);
    resp.body = data.substr(header_end + 4);

    size_t line_end = head.find("\r\n");
    std::string status_line = head.substr(0, line_end);
    size_t sp1 = status_line.find(' ');
    if (sp1 != std::string::npos) {
        size_t sp2 = status_line.find(' ', sp1 + 1);
        std::string code_str = (sp2 == std::string::npos)
                                   ? status_line.substr(sp1 + 1)
                                   : status_line.substr(sp1 + 1, sp2 - sp1 - 1);
        try {
            resp.status = std::stoi(code_str);
        } catch (...) {
            resp.status = 0;
        }
    }

    size_t pos = (line_end == std::string::npos) ? head.size() : line_end + 2;
    while (pos < head.size()) {
        size_t e = head.find("\r\n", pos);
        std::string line = (e == std::string::npos) ? head.substr(pos)
                                                     : head.substr(pos, e - pos);
        pos = (e == std::string::npos) ? head.size() : e + 2;

        size_t colon = line.find(':');
        if (colon == std::string::npos) continue;

        std::string key = line.substr(0, colon);
        std::string val = line.substr(colon + 1);
        while (!val.empty() && (val[0] == ' ' || val[0] == '\t')) val.erase(val.begin());
        std::transform(key.begin(), key.end(), key.begin(),
                       [](unsigned char c) { return std::tolower(c); });
        resp.headers[key] = val;
    }

    return resp;
}

ClientResponse get(const std::string& url, int timeout_ms) {
    return request("GET", url, "", {}, timeout_ms);
}

ClientResponse post_json(const std::string& url, const std::string& json_body,
                         int timeout_ms) {
    return request("POST", url, json_body,
                   {{"Content-Type", "application/json"}}, timeout_ms);
}

}
