#ifndef CORE_INTERFACES_HTTP_TRANSPORT_H
#define CORE_INTERFACES_HTTP_TRANSPORT_H

#include <functional>
#include <string>
#include <utility>
#include <vector>

struct HttpRequest {
    std::string method = "POST";
    std::string url;
    std::vector<std::pair<std::string, std::string>> headers;
    std::string body;
    int timeout_ms = 300000;
};

struct HttpResponse {
    int status_code = 0;
    std::string body;
    std::string error;
};

class IHttpTransport {
public:
    virtual ~IHttpTransport() = default;

    virtual HttpResponse send(const HttpRequest& request) = 0;

    // Streaming: on_chunk receives each raw data chunk as it arrives.
    // Return false from on_chunk to abort the stream.
    virtual HttpResponse send_streaming(
        const HttpRequest& request,
        std::function<bool(const std::string& chunk)> on_chunk) = 0;
};

#endif
