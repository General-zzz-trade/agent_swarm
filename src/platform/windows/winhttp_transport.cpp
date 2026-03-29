#include "winhttp_transport.h"

#include <sstream>
#include <stdexcept>
#include <vector>

namespace {

constexpr const wchar_t* kUserAgent = L"Bolt/1.0";

class WinHttpHandle {
public:
    explicit WinHttpHandle(HINTERNET handle = nullptr) : handle_(handle) {}
    ~WinHttpHandle() {
        if (handle_ != nullptr) {
            WinHttpCloseHandle(handle_);
        }
    }

    WinHttpHandle(const WinHttpHandle&) = delete;
    WinHttpHandle& operator=(const WinHttpHandle&) = delete;

    WinHttpHandle(WinHttpHandle&& other) noexcept : handle_(other.handle_) {
        other.handle_ = nullptr;
    }

    WinHttpHandle& operator=(WinHttpHandle&& other) noexcept {
        if (this != &other) {
            if (handle_ != nullptr) {
                WinHttpCloseHandle(handle_);
            }
            handle_ = other.handle_;
            other.handle_ = nullptr;
        }
        return *this;
    }

    HINTERNET get() const { return handle_; }

private:
    HINTERNET handle_;
};

std::wstring utf8_to_wide(const std::string& value) {
    if (value.empty()) {
        return L"";
    }
    const int required =
        MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, nullptr, 0);
    if (required <= 0) {
        throw std::runtime_error("UTF-8 to wide conversion failed");
    }
    std::wstring result(static_cast<std::size_t>(required), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, result.data(), required);
    result.pop_back();
    return result;
}

std::string wide_to_utf8(const std::wstring& value) {
    if (value.empty()) {
        return "";
    }
    const int required =
        WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (required <= 0) {
        return "";
    }
    std::string result(static_cast<std::size_t>(required), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, result.data(), required, nullptr, nullptr);
    result.pop_back();
    return result;
}

std::string format_windows_error(const std::string& context) {
    const DWORD error_code = GetLastError();
    wchar_t* message_buffer = nullptr;
    const DWORD length = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
            FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, error_code,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<LPWSTR>(&message_buffer), 0, nullptr);

    std::wstring message;
    if (length > 0 && message_buffer != nullptr) {
        message.assign(message_buffer, length);
        LocalFree(message_buffer);
    }

    while (!message.empty() &&
           (message.back() == L'\r' || message.back() == L'\n' || message.back() == L' ')) {
        message.pop_back();
    }

    std::ostringstream output;
    output << context << " (error " << error_code;
    if (!message.empty()) {
        output << ": " << wide_to_utf8(message);
    }
    output << ")";
    return output.str();
}

}  // namespace

WinHttpTransport::WinHttpTransport() {
    session_ = WinHttpOpen(kUserAgent, WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                           WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (session_ == nullptr) {
        throw std::runtime_error(format_windows_error("Failed to open WinHTTP session"));
    }
}

WinHttpTransport::~WinHttpTransport() {
    for (auto& pair : connections_) {
        if (pair.second != nullptr) {
            WinHttpCloseHandle(pair.second);
        }
    }
    connections_.clear();

    if (session_ != nullptr) {
        WinHttpCloseHandle(session_);
    }
}

WinHttpTransport::ParsedUrl WinHttpTransport::parse_url(const std::string& url) {
    ParsedUrl result;

    std::string remaining = url;
    if (remaining.rfind("https://", 0) == 0) {
        result.secure = true;
        result.port = 443;
        remaining = remaining.substr(8);
    } else if (remaining.rfind("http://", 0) == 0) {
        result.secure = false;
        result.port = 80;
        remaining = remaining.substr(7);
    } else {
        throw std::runtime_error("URL must start with http:// or https://");
    }

    const auto path_start = remaining.find('/');
    std::string host_port;
    std::string path;
    if (path_start == std::string::npos) {
        host_port = remaining;
        path = "/";
    } else {
        host_port = remaining.substr(0, path_start);
        path = remaining.substr(path_start);
    }

    const auto colon = host_port.find(':');
    if (colon != std::string::npos) {
        result.host = utf8_to_wide(host_port.substr(0, colon));
        result.port = static_cast<unsigned short>(std::stoi(host_port.substr(colon + 1)));
    } else {
        result.host = utf8_to_wide(host_port);
    }

    result.path = utf8_to_wide(path);
    return result;
}

HINTERNET WinHttpTransport::get_connection(const ParsedUrl& parsed) {
    const std::string key = wide_to_utf8(parsed.host) + ":" + std::to_string(parsed.port);

    std::lock_guard<std::mutex> lock(connections_mutex_);
    auto it = connections_.find(key);
    if (it != connections_.end() && it->second != nullptr) {
        return it->second;
    }

    HINTERNET connection = WinHttpConnect(
        session_, parsed.host.c_str(), static_cast<INTERNET_PORT>(parsed.port), 0);
    if (connection == nullptr) {
        throw std::runtime_error(format_windows_error("Failed to connect to " + key));
    }

    connections_[key] = connection;
    return connection;
}

HttpResponse WinHttpTransport::send(const HttpRequest& request) {
    return execute_request(request, nullptr);
}

HttpResponse WinHttpTransport::send_streaming(
    const HttpRequest& request,
    std::function<bool(const std::string& chunk)> on_chunk) {
    return execute_request(request, &on_chunk);
}

HttpResponse WinHttpTransport::execute_request(
    const HttpRequest& request,
    std::function<bool(const std::string& chunk)>* on_chunk) {

    const ParsedUrl parsed = parse_url(request.url);
    HINTERNET connection = get_connection(parsed);

    const std::wstring method = utf8_to_wide(request.method);
    DWORD flags = parsed.secure ? WINHTTP_FLAG_SECURE : 0;

    WinHttpHandle req(WinHttpOpenRequest(
        connection, method.c_str(), parsed.path.c_str(), nullptr,
        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags));
    if (req.get() == nullptr) {
        return {0, "", format_windows_error("Failed to create request")};
    }

    // Set timeouts
    if (request.timeout_ms > 0) {
        WinHttpSetTimeouts(req.get(), 5000, 10000, request.timeout_ms, request.timeout_ms);
    }

    // Build headers
    std::wstring all_headers = L"Content-Type: application/json\r\n";
    for (const auto& header : request.headers) {
        all_headers += utf8_to_wide(header.first) + L": " + utf8_to_wide(header.second) + L"\r\n";
    }

    if (!WinHttpSendRequest(req.get(), all_headers.c_str(),
                            static_cast<DWORD>(-1L),
                            const_cast<char*>(request.body.data()),
                            static_cast<DWORD>(request.body.size()),
                            static_cast<DWORD>(request.body.size()), 0)) {
        return {0, "", format_windows_error("Failed to send request")};
    }

    if (!WinHttpReceiveResponse(req.get(), nullptr)) {
        return {0, "", format_windows_error("Failed to receive response")};
    }

    // Query status code
    DWORD status_code = 0;
    DWORD status_size = sizeof(status_code);
    WinHttpQueryHeaders(req.get(),
                        WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                        WINHTTP_HEADER_NAME_BY_INDEX, &status_code, &status_size,
                        WINHTTP_NO_HEADER_INDEX);

    // Read response body
    HttpResponse response;
    response.status_code = static_cast<int>(status_code);

    while (true) {
        DWORD available_bytes = 0;
        if (!WinHttpQueryDataAvailable(req.get(), &available_bytes)) {
            break;
        }
        if (available_bytes == 0) {
            break;
        }

        std::vector<char> buffer(available_bytes);
        DWORD bytes_read = 0;
        if (!WinHttpReadData(req.get(), buffer.data(), available_bytes, &bytes_read)) {
            break;
        }

        std::string chunk(buffer.data(), bytes_read);

        if (on_chunk != nullptr) {
            response.body += chunk;
            if (!(*on_chunk)(chunk)) {
                break;  // Caller requested abort
            }
        } else {
            response.body += chunk;
        }
    }

    return response;
}
