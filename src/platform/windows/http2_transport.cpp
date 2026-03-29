#include "http2_transport.h"

#include <algorithm>
#include <future>
#include <sstream>

namespace {

std::wstring to_wide(const std::string& str) {
    if (str.empty()) return {};
    const int size = MultiByteToWideChar(CP_UTF8, 0, str.data(),
                                         static_cast<int>(str.size()), nullptr, 0);
    std::wstring result(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, str.data(), static_cast<int>(str.size()),
                        result.data(), size);
    return result;
}

std::string from_wide(const std::wstring& wstr) {
    if (wstr.empty()) return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, wstr.data(),
                                         static_cast<int>(wstr.size()),
                                         nullptr, 0, nullptr, nullptr);
    std::string result(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wstr.data(), static_cast<int>(wstr.size()),
                        result.data(), size, nullptr, nullptr);
    return result;
}

}  // namespace

Http2Transport::Http2Transport(std::size_t max_connections_per_host)
    : max_connections_per_host_(max_connections_per_host) {
    session_ = WinHttpOpen(L"AgentSwarm/2.0",
                           WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                           WINHTTP_NO_PROXY_NAME,
                           WINHTTP_NO_PROXY_BYPASS, 0);
    if (session_ == nullptr) {
        throw std::runtime_error("Failed to create WinHTTP session for HTTP/2");
    }

    // Enable HTTP/2
    DWORD http2_option = WINHTTP_PROTOCOL_FLAG_HTTP2;
    WinHttpSetOption(session_, WINHTTP_OPTION_ENABLE_HTTP_PROTOCOL,
                     &http2_option, sizeof(http2_option));

    // Enable connection pooling
    DWORD max_connections = static_cast<DWORD>(max_connections_per_host_);
    WinHttpSetOption(session_, WINHTTP_OPTION_MAX_CONNS_PER_SERVER,
                     &max_connections, sizeof(max_connections));
}

Http2Transport::~Http2Transport() {
    std::lock_guard<std::mutex> lock(pool_mutex_);
    for (auto& [key, slots] : pool_) {
        for (auto& slot : slots) {
            if (slot.connection) {
                WinHttpCloseHandle(slot.connection);
            }
        }
    }
    if (session_) {
        WinHttpCloseHandle(session_);
    }
}

Http2Transport::ParsedUrl Http2Transport::parse_url(const std::string& url) {
    ParsedUrl parsed;
    std::string remaining = url;

    if (remaining.rfind("https://", 0) == 0) {
        parsed.secure = true;
        parsed.port = 443;
        remaining = remaining.substr(8);
    } else if (remaining.rfind("http://", 0) == 0) {
        parsed.secure = false;
        parsed.port = 80;
        remaining = remaining.substr(7);
    } else {
        parsed.secure = true;
        parsed.port = 443;
    }

    const std::size_t path_start = remaining.find('/');
    const std::string host_part = remaining.substr(0, path_start);
    const std::string path_part = path_start != std::string::npos
                                      ? remaining.substr(path_start)
                                      : "/";

    const std::size_t colon = host_part.find(':');
    if (colon != std::string::npos) {
        parsed.host = to_wide(host_part.substr(0, colon));
        parsed.port = static_cast<unsigned short>(std::stoi(host_part.substr(colon + 1)));
    } else {
        parsed.host = to_wide(host_part);
    }

    parsed.path = to_wide(path_part);
    parsed.host_key = from_wide(parsed.host) + ":" + std::to_string(parsed.port);
    return parsed;
}

Http2Transport::ConnectionSlot& Http2Transport::acquire_connection(const ParsedUrl& parsed) {
    std::lock_guard<std::mutex> lock(pool_mutex_);
    auto& slots = pool_[parsed.host_key];

    // Find slot with least active requests
    ConnectionSlot* best = nullptr;
    int min_active = std::numeric_limits<int>::max();
    for (auto& slot : slots) {
        if (slot.active_requests < min_active) {
            min_active = slot.active_requests;
            best = &slot;
        }
    }

    // Create new slot if under limit
    if (best == nullptr || (min_active > 0 && slots.size() < max_connections_per_host_)) {
        HINTERNET conn = WinHttpConnect(session_, parsed.host.c_str(),
                                        parsed.port, 0);
        if (conn == nullptr) {
            throw std::runtime_error("Failed to create HTTP/2 connection");
        }
        slots.push_back({conn, 0});
        best = &slots.back();
    }

    ++best->active_requests;
    return *best;
}

void Http2Transport::release_connection(const std::string& host_key, ConnectionSlot& slot) {
    std::lock_guard<std::mutex> lock(pool_mutex_);
    --slot.active_requests;
}

HttpResponse Http2Transport::send(const HttpRequest& request) {
    return execute_request(request, nullptr);
}

HttpResponse Http2Transport::send_streaming(
    const HttpRequest& request,
    std::function<bool(const std::string& chunk)> on_chunk) {
    return execute_request(request, &on_chunk);
}

std::vector<HttpResponse> Http2Transport::send_batch(const std::vector<HttpRequest>& requests) {
    if (requests.empty()) return {};

    // Launch all requests concurrently using std::async
    std::vector<std::future<HttpResponse>> futures;
    futures.reserve(requests.size());
    for (const auto& req : requests) {
        futures.push_back(std::async(std::launch::async, [this, &req]() {
            return execute_request(req, nullptr);
        }));
    }

    std::vector<HttpResponse> responses;
    responses.reserve(futures.size());
    for (auto& f : futures) {
        responses.push_back(f.get());
    }
    return responses;
}

HttpResponse Http2Transport::execute_request(
    const HttpRequest& request,
    std::function<bool(const std::string& chunk)>* on_chunk) {

    const ParsedUrl parsed = parse_url(request.url);
    auto& slot = acquire_connection(parsed);

    HttpResponse response;
    response.status_code = 0;

    const std::wstring method_wide = to_wide(request.method.empty() ? "POST" : request.method);
    DWORD flags = parsed.secure ? WINHTTP_FLAG_SECURE : 0;

    HINTERNET req_handle = WinHttpOpenRequest(
        slot.connection, method_wide.c_str(), parsed.path.c_str(),
        nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);

    if (req_handle == nullptr) {
        release_connection(parsed.host_key, slot);
        response.error = "Failed to create HTTP request";
        return response;
    }

    // Set timeout
    const int timeout_ms = request.timeout_ms > 0 ? request.timeout_ms : 60000;
    WinHttpSetTimeouts(req_handle, timeout_ms, timeout_ms, timeout_ms, timeout_ms);

    // Build headers
    std::wstring headers_str;
    for (const auto& [key, value] : request.headers) {
        headers_str += to_wide(key) + L": " + to_wide(value) + L"\r\n";
    }

    // Send request
    const DWORD body_length = static_cast<DWORD>(request.body.size());
    BOOL sent = WinHttpSendRequest(
        req_handle, headers_str.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : headers_str.c_str(),
        static_cast<DWORD>(headers_str.size()),
        request.body.empty() ? WINHTTP_NO_REQUEST_DATA
                             : const_cast<char*>(request.body.data()),
        body_length, body_length, 0);

    if (!sent || !WinHttpReceiveResponse(req_handle, nullptr)) {
        WinHttpCloseHandle(req_handle);
        release_connection(parsed.host_key, slot);
        response.error = "HTTP request failed";
        return response;
    }

    // Read status code
    DWORD status_code = 0;
    DWORD status_size = sizeof(status_code);
    WinHttpQueryHeaders(req_handle,
                        WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                        WINHTTP_HEADER_NAME_BY_INDEX, &status_code, &status_size,
                        WINHTTP_NO_HEADER_INDEX);
    response.status_code = static_cast<int>(status_code);

    // Read body
    std::string body;
    DWORD available = 0;
    while (WinHttpQueryDataAvailable(req_handle, &available) && available > 0) {
        std::string chunk(available, '\0');
        DWORD read = 0;
        if (WinHttpReadData(req_handle, chunk.data(), available, &read)) {
            chunk.resize(read);
            if (on_chunk) {
                if (!(*on_chunk)(chunk)) {
                    break;  // Caller aborted
                }
            }
            body += chunk;
        } else {
            break;
        }
    }

    response.body = std::move(body);
    WinHttpCloseHandle(req_handle);
    release_connection(parsed.host_key, slot);
    return response;
}

std::size_t Http2Transport::active_connections() const {
    std::lock_guard<std::mutex> lock(pool_mutex_);
    std::size_t total = 0;
    for (const auto& [key, slots] : pool_) {
        total += slots.size();
    }
    return total;
}
