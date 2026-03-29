#ifndef PLATFORM_LINUX_LINUX_HTTP_TRANSPORT_H
#define PLATFORM_LINUX_LINUX_HTTP_TRANSPORT_H

#include <mutex>
#include <string>
#include <unordered_map>

#include "../../core/interfaces/http_transport.h"

/// POSIX socket-based HTTP transport. Supports HTTP and HTTPS (via OpenSSL if available).
/// Zero external dependencies for HTTP; OpenSSL optional for HTTPS.
class LinuxHttpTransport : public IHttpTransport {
public:
    LinuxHttpTransport();
    ~LinuxHttpTransport();

    HttpResponse send(const HttpRequest& request) override;
    HttpResponse send_streaming(
        const HttpRequest& request,
        std::function<bool(const std::string& chunk)> on_chunk) override;

private:
    struct ParsedUrl {
        bool secure = false;
        std::string host;
        int port = 80;
        std::string path;
    };

    static ParsedUrl parse_url(const std::string& url);
    HttpResponse execute_request(const HttpRequest& request,
                                 std::function<bool(const std::string& chunk)>* on_chunk);
    HttpResponse execute_via_curl(const HttpRequest& request,
                                  std::function<bool(const std::string& chunk)>* on_chunk);

    // Simple connection cache
    std::mutex mutex_;
    std::unordered_map<std::string, int> connections_;  // host:port -> fd
};

#endif
