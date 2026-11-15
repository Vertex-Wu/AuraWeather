#pragma once

#include <string>

struct HttpResult {
    int status = 0;
    std::string body;
    std::string error;
    std::string contentType;
    std::string location;
    bool ok() const { return status >= 200 && status < 300 && error.empty(); }
    bool success() const { return status >= 200 && status < 300; }
};

class Http {
public:
    static bool init(const std::string& caPem, std::string& err);
    static HttpResult get(const std::string& url, int timeoutMs = 30000);
    static const char* userAgent();
};

