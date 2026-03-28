#ifndef PLATFORM_WINDOWS_WINHTTP_TRANSPORT_H
#define PLATFORM_WINDOWS_WINHTTP_TRANSPORT_H

#include <mutex>
#include <string>
#include <unordered_map>

#include <windows.h>
#include <winhttp.h>

#include "../../core/interfaces/http_transport.h"

class WinHttpTransport : public IHttpTransport {
public:
    WinHttpTransport();
    ~WinHttpTransport();

    WinHttpTransport(const WinHttpTransport&) = delete;
    WinHttpTransport& operator=(const WinHttpTransport&) = delete;

    HttpResponse send(const HttpRequest& request) override;
    HttpResponse send_streaming(
        const HttpRequest& request,
        std::function<bool(const std::string& chunk)> on_chunk) override;

private:
    struct ParsedUrl {
        bool secure = false;
        std::wstring host;
        unsigned short port = 443;
        std::wstring path;
    };

    HINTERNET session_;
    std::mutex connections_mutex_;
    std::unordered_map<std::string, HINTERNET> connections_;

    static ParsedUrl parse_url(const std::string& url);
    HINTERNET get_connection(const ParsedUrl& parsed);
    HttpResponse execute_request(const HttpRequest& request,
                                 std::function<bool(const std::string& chunk)>* on_chunk);
};

#endif
