#ifndef PLATFORM_LINUX_LINUX_HTTP_TRANSPORT_H
#define PLATFORM_LINUX_LINUX_HTTP_TRANSPORT_H

#include <mutex>
#include <string>

#include <curl/curl.h>

#include "../../core/interfaces/http_transport.h"

/// libcurl-based HTTP transport with persistent connection pooling and HTTP/2.
///
/// Performance features:
/// - Connection reuse via CURLOPT_FORBID_REUSE=0 (keep-alive by default)
/// - HTTP/2 multiplexing when available (CURL_HTTP_VERSION_2TLS)
/// - DNS cache (60s TTL inside curl multi handle)
/// - TCP_NODELAY for minimal latency
/// - Zero temporary files (body sent via CURLOPT_POSTFIELDS)
/// - Thread-safe: one CURL easy handle per request, shared connection pool
class LinuxHttpTransport : public IHttpTransport {
public:
    LinuxHttpTransport();
    ~LinuxHttpTransport();

    HttpResponse send(const HttpRequest& request) override;
    HttpResponse send_streaming(
        const HttpRequest& request,
        std::function<bool(const std::string& chunk)> on_chunk) override;

private:
    // Shared connection cache (curl share handle)
    CURLSH* share_handle_ = nullptr;
    std::mutex share_dns_mutex_;
    std::mutex share_conn_mutex_;
    std::mutex share_ssl_mutex_;

    // Static callbacks for share handle locking
    static void share_lock_cb(CURL* handle, curl_lock_data data,
                              curl_lock_access access, void* userptr);
    static void share_unlock_cb(CURL* handle, curl_lock_data data, void* userptr);

    // Configure common CURL options
    void configure_handle(CURL* curl, const HttpRequest& request,
                          struct curl_slist** headers_list) const;

    // Response collection callbacks
    static size_t write_callback(char* ptr, size_t size, size_t nmemb, void* userdata);
    static size_t header_callback(char* ptr, size_t size, size_t nmemb, void* userdata);

    // Streaming callback context
    struct StreamContext {
        std::function<bool(const std::string& chunk)>* on_chunk = nullptr;
        bool aborted = false;
    };
    static size_t stream_write_callback(char* ptr, size_t size, size_t nmemb, void* userdata);
};

#endif
