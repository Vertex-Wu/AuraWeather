#include "http.h"
#include "common.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/x509_vfy.h>
#include <openssl/pem.h>
#include <zlib.h>

#include <mutex>
#include <vector>
#include <cstring>
#include <cstdio>
#include <algorithm>

#pragma comment(lib, "ws2_32.lib")

static SSL_CTX* g_ctx = nullptr;
static std::once_flag g_once;
static std::string g_initError;
static bool g_started = false;

struct ParsedUrl {
    std::string scheme, host, path, port;
    bool tls = false;
    int portNum = 80;
};

static bool parseUrl(const std::string& url, ParsedUrl& u, std::string& err) {
    size_t p = url.find("://");
    if (p == std::string::npos) { err = "无效的网址"; return false; }
    u.scheme = lowerAscii(url.substr(0, p));
    u.tls = (u.scheme == "https");
    if (!u.tls && u.scheme != "http") { err = "不支持的协议"; return false; }
    size_t start = p + 3;
    size_t slash = url.find('/', start);
    std::string hostport = url.substr(start, slash == std::string::npos ? std::string::npos : slash - start);
    u.path = slash == std::string::npos ? "/" : url.substr(slash);
    if (hostport.empty()) { err = "网址缺少主机名"; return false; }
    if (hostport[0] == '[') {
        size_t e = hostport.find(']');
        if (e == std::string::npos) { err = "无效的主机名"; return false; }
        u.host = hostport.substr(1, e - 1);
        if (e + 1 < hostport.size() && hostport[e + 1] == ':') u.port = hostport.substr(e + 2);
    } else {
        size_t c = hostport.rfind(':');
        if (c != std::string::npos) { u.host = hostport.substr(0, c); u.port = hostport.substr(c + 1); }
        else u.host = hostport;
    }
    if (u.host.empty()) { err = "网址缺少主机名"; return false; }
    u.portNum = u.tls ? 443 : 80;
    if (!u.port.empty()) {
        u.portNum = atoi(u.port.c_str());
        if (u.portNum <= 0 || u.portNum > 65535) { err = "无效的端口"; return false; }
    }
    return true;
}

static std::string lowerHeader(std::string s) {
    for (char& c : s) if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
    return s;
}

static size_t findHeaderEnd(const std::string& s) {
    size_t p = s.find("\r\n\r\n");
    if (p == std::string::npos) p = s.find("\n\n");
    return p;
}

static bool inflateGzip(const std::string& in, std::string& out, std::string& err) {
    z_stream zs{};
    if (inflateInit2(&zs, 16 + MAX_WBITS) != Z_OK) { err = "解压初始化失败"; return false; }
    zs.next_in = (Bytef*)in.data();
    zs.avail_in = (uInt)in.size();
    char buf[16384];
    int ret = Z_OK;
    while (ret != Z_STREAM_END) {
        zs.next_out = (Bytef*)buf;
        zs.avail_out = sizeof(buf);
        ret = inflate(&zs, Z_NO_FLUSH);
        if (ret != Z_OK && ret != Z_STREAM_END) { inflateEnd(&zs); err = "响应解压失败"; return false; }
        out.append(buf, sizeof(buf) - zs.avail_out);
        if (out.size() > 40 * 1024 * 1024) { inflateEnd(&zs); err = "响应内容过大"; return false; }
    }
    inflateEnd(&zs);
    return true;
}

static int connectWithTimeout(SOCKET s, const sockaddr* addr, int len, int timeoutMs) {
    u_long nb = 1;
    ioctlsocket(s, FIONBIO, &nb);
    int rc = connect(s, addr, len);
    if (rc == SOCKET_ERROR) {
        int e = WSAGetLastError();
        if (e != WSAEWOULDBLOCK && e != WSAEINPROGRESS && e != WSAEINVAL) return -1;
        fd_set wf;
        FD_ZERO(&wf);
        FD_SET(s, &wf);
        timeval tv{ timeoutMs / 1000, (timeoutMs % 1000) * 1000 };
        rc = select(0, nullptr, &wf, nullptr, &tv);
        if (rc <= 0) return -1;
        int soerr = 0;
        int sl = sizeof(soerr);
        if (getsockopt(s, SOL_SOCKET, SO_ERROR, (char*)&soerr, &sl) != 0 || soerr != 0) return -1;
    }
    nb = 0;
    ioctlsocket(s, FIONBIO, &nb);
    return 0;
}

static bool loadCa(SSL_CTX* ctx, const std::string& pem, std::string& err) {
    SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER, nullptr);
    SSL_CTX_set_default_verify_paths(ctx);
    if (pem.empty()) { err = "证书数据为空"; return false; }
    BIO* bio = BIO_new_mem_buf(pem.data(), (int)pem.size());
    if (!bio) { err = "证书内存分配失败"; return false; }
    int added = 0;
    X509_STORE* store = SSL_CTX_get_cert_store(ctx);
    while (true) {
        X509* cert = PEM_read_bio_X509(bio, nullptr, nullptr, nullptr);
        if (!cert) break;
        if (X509_STORE_add_cert(store, cert) == 1) added++;
        X509_free(cert);
    }
    BIO_free(bio);
    ERR_clear_error();
    if (added <= 0) { err = "未能加载根证书"; return false; }
    return true;
}

static void ensureInit() {
    std::call_once(g_once, []() {
        WSADATA wsa{};
        if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) { g_initError = "网络初始化失败"; return; }
        g_started = true;
        OPENSSL_init_ssl(OPENSSL_INIT_LOAD_SSL_STRINGS | OPENSSL_INIT_LOAD_CRYPTO_STRINGS, nullptr);
        g_ctx = SSL_CTX_new(TLS_client_method());
        if (!g_ctx) { g_initError = "TLS 初始化失败"; return; }
        SSL_CTX_set_min_proto_version(g_ctx, TLS1_2_VERSION);
        SSL_CTX_set_mode(g_ctx, SSL_MODE_AUTO_RETRY);
        SSL_CTX_set_options(g_ctx, SSL_OP_NO_COMPRESSION);
        if (!g_initError.empty()) return;
    });
}

bool Http::init(const std::string& caPem, std::string& err) {
    ensureInit();
    if (!g_started) { err = g_initError.empty() ? "网络初始化失败" : g_initError; return false; }
    if (!g_ctx) { err = g_initError.empty() ? "TLS 初始化失败" : g_initError; return false; }
    static std::once_flag caOnce;
    static bool caOk = false;
    static std::string caErr;
    std::call_once(caOnce, [&]() {
        caOk = loadCa(g_ctx, caPem, caErr);
    });
    if (!caOk) { err = caErr; return false; }
    return true;
}

const char* Http::userAgent() {
    return "AuraWeather/1.0 (Windows desktop app; contact: auraweather@qq.com)";
}

static HttpResult requestOnce(const ParsedUrl& u, const std::string& url, int timeoutMs) {
    HttpResult r;
    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    addrinfo* res = nullptr;
    char port[16];
    snprintf(port, sizeof port, "%d", u.portNum);
    int gai = getaddrinfo(u.host.c_str(), port, &hints, &res);
    if (gai != 0 || !res) { r.error = "无法解析服务器地址"; return r; }

    SOCKET s = INVALID_SOCKET;
    for (addrinfo* ai = res; ai; ai = ai->ai_next) {
        s = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (s == INVALID_SOCKET) continue;
        if (connectWithTimeout(s, ai->ai_addr, (int)ai->ai_addrlen, timeoutMs) == 0) break;
        closesocket(s);
        s = INVALID_SOCKET;
    }
    freeaddrinfo(res);
    if (s == INVALID_SOCKET) { r.error = "无法连接服务器"; return r; }

    DWORD tv = (DWORD)timeoutMs;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (char*)&tv, sizeof(tv));
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, (char*)&tv, sizeof(tv));

    SSL* ssl = nullptr;
    if (u.tls) {
        ssl = SSL_new(g_ctx);
        if (!ssl) { closesocket(s); r.error = "TLS 会话创建失败"; return r; }
        SSL_set_tlsext_host_name(ssl, u.host.c_str());
        SSL_set1_host(ssl, u.host.c_str());
        SSL_set_fd(ssl, (int)s);
        int rc = SSL_connect(ssl);
        if (rc != 1) {
            long vr = SSL_get_verify_result(ssl);
            if (vr != X509_V_OK) r.error = std::string("证书验证失败: ") + X509_verify_cert_error_string(vr);
            else r.error = "安全连接失败";
            SSL_free(ssl);
            closesocket(s);
            return r;
        }
        long vr = SSL_get_verify_result(ssl);
        if (vr != X509_V_OK) {
            r.error = std::string("证书验证失败: ") + X509_verify_cert_error_string(vr);
            SSL_free(ssl);
            closesocket(s);
            return r;
        }
    }

    std::string hostHeader = u.host;
    if ((u.tls && u.portNum != 443) || (!u.tls && u.portNum != 80))
        hostHeader += ":" + std::to_string(u.portNum);
    std::string req = "GET " + u.path + " HTTP/1.1\r\n";
    req += "Host: " + hostHeader + "\r\n";
    req += std::string("User-Agent: ") + Http::userAgent() + "\r\n";
    req += "Accept: application/json,image/*,*/*\r\n";
    req += "Accept-Encoding: identity\r\n";
    req += "Connection: close\r\n\r\n";

    auto sendAll = [&](const char* data, int len) -> bool {
        if (!ssl) {
            int sent = 0;
            while (sent < len) {
                int n = send(s, data + sent, len - sent, 0);
                if (n <= 0) return false;
                sent += n;
            }
            return true;
        }
        int sent = 0;
        while (sent < len) {
            int n = SSL_write(ssl, data + sent, len - sent);
            if (n <= 0) return false;
            sent += n;
        }
        return true;
    };

    if (!sendAll(req.data(), (int)req.size())) {
        r.error = "请求发送失败";
        if (ssl) SSL_free(ssl);
        closesocket(s);
        return r;
    }

    std::string raw;
    char buf[32768];
    while (true) {
        int n;
        if (ssl) n = SSL_read(ssl, buf, sizeof(buf));
        else n = recv(s, buf, sizeof(buf), 0);
        if (n > 0) {
            raw.append(buf, n);
            if (raw.size() > 48 * 1024 * 1024) { r.error = "响应内容过大"; break; }
        } else {
            if (n < 0) {
                int e = ssl ? SSL_get_error(ssl, n) : WSAGetLastError();
                if (ssl && (e == SSL_ERROR_ZERO_RETURN)) break;
                if (!ssl && e == WSAETIMEDOUT && !raw.empty()) break;
                if (raw.empty()) r.error = "读取响应超时";
            }
            break;
        }
    }
    if (ssl) {
        SSL_shutdown(ssl);
        SSL_free(ssl);
    }
    closesocket(s);

    if (raw.empty()) { if (r.error.empty()) r.error = "服务器未返回内容"; return r; }
    size_t he = findHeaderEnd(raw);
    size_t sep = 4;
    if (he == std::string::npos) { he = raw.find("\n\n"); sep = 2; }
    if (he == std::string::npos) { r.error = "无效的 HTTP 响应"; return r; }
    std::string head = raw.substr(0, he);
    std::string body = raw.substr(he + sep);
    size_t lineEnd = head.find('\n');
    std::string statusLine = head.substr(0, lineEnd);
    if (statusLine.rfind("HTTP/", 0) != 0) { r.error = "无效的 HTTP 状态行"; return r; }
    size_t sp = statusLine.find(' ');
    if (sp != std::string::npos) r.status = atoi(statusLine.c_str() + sp + 1);

    bool chunked = false;
    bool gzipped = false;
    long long contentLen = -1;
    size_t p = lineEnd == std::string::npos ? head.size() : lineEnd + 1;
    while (p < head.size()) {
        size_t e = head.find('\n', p);
        std::string line = head.substr(p, e == std::string::npos ? std::string::npos : e - p);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        size_t c = line.find(':');
        if (c != std::string::npos) {
            std::string name = lowerHeader(trim(line.substr(0, c)));
            std::string val = trim(line.substr(c + 1));
            if (name == "content-length") contentLen = atoll(val.c_str());
            else if (name == "transfer-encoding" && lowerHeader(val).find("chunked") != std::string::npos) chunked = true;
            else if (name == "content-encoding" && lowerHeader(val).find("gzip") != std::string::npos) gzipped = true;
            else if (name == "content-type") r.contentType = val;
            else if (name == "location") r.location = val;
        }
        if (e == std::string::npos) break;
        p = e + 1;
    }

    if (chunked) {
        std::string dec;
        size_t pos = 0;
        while (pos < body.size()) {
            size_t e = body.find("\r\n", pos);
            if (e == std::string::npos) break;
            std::string sz = body.substr(pos, e - pos);
            size_t semi = sz.find(';');
            if (semi != std::string::npos) sz = sz.substr(0, semi);
            long n = strtol(sz.c_str(), nullptr, 16);
            if (n <= 0) break;
            pos = e + 2;
            if (pos + (size_t)n > body.size()) { dec += body.substr(pos); break; }
            dec += body.substr(pos, (size_t)n);
            pos += (size_t)n + 2;
        }
        body.swap(dec);
    } else if (contentLen >= 0 && body.size() > (size_t)contentLen) {
        body.resize((size_t)contentLen);
    }

    if (gzipped) {
        std::string dec;
        if (inflateGzip(body, dec, r.error)) body.swap(dec);
    }
    r.body.swap(body);
    return r;
}

HttpResult Http::get(const std::string& url, int timeoutMs) {
    ensureInit();
    if (!g_started || !g_ctx) {
        HttpResult r;
        r.error = g_initError.empty() ? "网络初始化失败" : g_initError;
        return r;
    }
    std::string current = url;
    for (int redirects = 0; redirects < 5; redirects++) {
        ParsedUrl u;
        std::string err;
        if (!parseUrl(current, u, err)) { HttpResult r; r.error = err; return r; }
        HttpResult r = requestOnce(u, current, timeoutMs);
        if (r.success() || r.status == 0) return r;
        if (
            (r.status == 301 || r.status == 302 || r.status == 303 || r.status == 307 || r.status == 308)
            && !r.location.empty()) {
            std::string loc = r.location;
            if (loc.rfind("http://", 0) == 0 || loc.rfind("https://", 0) == 0) current = loc;
            else if (!loc.empty() && loc[0] == '/') current = u.scheme + "://" + u.host + (u.port.empty() ? "" : ":" + u.port) + loc;
            else current = u.scheme + "://" + u.host + (u.port.empty() ? "" : ":" + u.port) + "/" + loc;
            continue;
        }
        return r;
    }
    HttpResult r;
    r.error = "重定向次数过多";
    return r;
}

