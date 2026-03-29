#ifndef PLATFORM_WINDOWS_HTTP2_TRANSPORT_H
#define PLATFORM_WINDOWS_HTTP2_TRANSPORT_H

#include <functional>
#include <limits>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include <windows.h>
#include <winhttp.h>

#include "../../core/interfaces/http_transport.h"

/// HTTP/2 multiplexing transport using WinHTTP with HTTP/2 enabled.
/// Supports multiple concurrent requests over a single TCP connection per host.
/// Falls back to HTTP/1.1 gracefully if server doesn't support HTTP/2.
class Http2Transport : public IHttpTransport {
public:
    explicit Http2Transport(std::size_t max_connections_per_host = 4);
    ~Http2Transport();

    Http2Transport(const Http2Transport&) = delete;
    Http2Transport& operator=(const Http2Transport&) = delete;

    HttpResponse send(const HttpRequest& request) override;
    HttpResponse send_streaming(
        const HttpRequest& request,
        std::function<bool(const std::string& chunk)> on_chunk) override;

    /// Send multiple requests concurrently, multiplexed over shared connections.
    std::vector<HttpResponse> send_batch(const std::vector<HttpRequest>& requests);

    /// Connection pool stats
    std::size_t active_connections() const;

private:
    struct ParsedUrl {
        bool secure = false;
        std::wstring host;
        unsigned short port = 443;
        std::wstring path;
        std::string host_key;  // "host:port" for connection pooling
    };

    struct ConnectionSlot {
        HINTERNET connection = nullptr;
        int active_requests = 0;  // protected by pool_mutex_
    };

    HINTERNET session_;
    std::size_t max_connections_per_host_;
    mutable std::mutex pool_mutex_;
    std::unordered_map<std::string, std::vector<ConnectionSlot>> pool_;

    static ParsedUrl parse_url(const std::string& url);
    ConnectionSlot& acquire_connection(const ParsedUrl& parsed);
    void release_connection(const std::string& host_key, ConnectionSlot& slot);
    HttpResponse execute_request(const HttpRequest& request,
                                 std::function<bool(const std::string& chunk)>* on_chunk);
};

#endif
