#include "windows_ollama_model_client.h"
#include "ollama_json_utils.h"

#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include <windows.h>
#include <winhttp.h>

namespace {

constexpr const wchar_t* kUserAgent = L"mini_nn_cpp/1.0";

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
        if (this == &other) {
            return *this;
        }
        if (handle_ != nullptr) {
            WinHttpCloseHandle(handle_);
        }
        handle_ = other.handle_;
        other.handle_ = nullptr;
        return *this;
    }

    HINTERNET get() const {
        return handle_;
    }

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
        throw std::runtime_error("Failed to convert UTF-8 string to wide string");
    }

    std::wstring result(static_cast<std::size_t>(required), L'\0');
    if (MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, result.data(), required) <= 0) {
        throw std::runtime_error("Failed to convert UTF-8 string to wide string");
    }
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
    if (WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, result.data(), required, nullptr,
                            nullptr) <= 0) {
        return "";
    }
    result.pop_back();
    return result;
}

std::string format_windows_error(const std::string& context) {
    const DWORD error_code = GetLastError();
    wchar_t* message_buffer = nullptr;
    const DWORD length = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
            FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr,
        error_code,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<LPWSTR>(&message_buffer),
        0,
        nullptr);

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

std::string query_status_text(DWORD status_code) {
    std::ostringstream output;
    output << "HTTP " << status_code;
    return output.str();
}

void validate_connection_config(const OllamaConnectionConfig& config) {
    if (config.host.empty()) {
        throw std::runtime_error("Ollama host must not be empty");
    }
    if (config.generate_path.empty() || config.generate_path.front() != '/') {
        throw std::runtime_error("Ollama path must start with '/'");
    }
    if (config.resolve_timeout_ms < 0 || config.connect_timeout_ms < 0 ||
        config.send_timeout_ms < 0 || config.receive_timeout_ms < 0) {
        throw std::runtime_error("Ollama timeouts must be >= 0");
    }
}

std::string post_json_to_ollama(const std::string& request_body,
                                const OllamaConnectionConfig& config) {
    validate_connection_config(config);

    WinHttpHandle session(
        WinHttpOpen(kUserAgent, WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME,
                    WINHTTP_NO_PROXY_BYPASS, 0));
    if (session.get() == nullptr) {
        throw std::runtime_error(format_windows_error("Failed to open WinHTTP session"));
    }

    if (!WinHttpSetTimeouts(session.get(), config.resolve_timeout_ms,
                            config.connect_timeout_ms, config.send_timeout_ms,
                            config.receive_timeout_ms)) {
        throw std::runtime_error(format_windows_error("Failed to configure WinHTTP timeouts"));
    }

    const std::wstring host = utf8_to_wide(config.host);
    const std::wstring path = utf8_to_wide(config.generate_path);

    WinHttpHandle connection(
        WinHttpConnect(session.get(), host.c_str(), static_cast<INTERNET_PORT>(config.port), 0));
    if (connection.get() == nullptr) {
        throw std::runtime_error(format_windows_error("Failed to connect to Ollama"));
    }

    WinHttpHandle request(WinHttpOpenRequest(connection.get(), L"POST", path.c_str(), nullptr,
                                             WINHTTP_NO_REFERER,
                                             WINHTTP_DEFAULT_ACCEPT_TYPES, 0));
    if (request.get() == nullptr) {
        throw std::runtime_error(format_windows_error("Failed to create HTTP request"));
    }

    const wchar_t* headers = L"Content-Type: application/json\r\n";
    if (!WinHttpSendRequest(request.get(), headers, -1L,
                            const_cast<char*>(request_body.data()),
                            static_cast<DWORD>(request_body.size()),
                            static_cast<DWORD>(request_body.size()), 0)) {
        throw std::runtime_error(format_windows_error("Failed to send request to Ollama"));
    }

    if (!WinHttpReceiveResponse(request.get(), nullptr)) {
        throw std::runtime_error(format_windows_error("Failed to receive response from Ollama"));
    }

    DWORD status_code = 0;
    DWORD status_code_size = sizeof(status_code);
    if (!WinHttpQueryHeaders(request.get(),
                             WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                             WINHTTP_HEADER_NAME_BY_INDEX, &status_code, &status_code_size,
                             WINHTTP_NO_HEADER_INDEX)) {
        throw std::runtime_error(format_windows_error("Failed to query HTTP status code"));
    }

    std::string response_body;
    while (true) {
        DWORD available_bytes = 0;
        if (!WinHttpQueryDataAvailable(request.get(), &available_bytes)) {
            throw std::runtime_error(format_windows_error("Failed while reading response body"));
        }
        if (available_bytes == 0) {
            break;
        }

        std::vector<char> buffer(available_bytes);
        DWORD bytes_read = 0;
        if (!WinHttpReadData(request.get(), buffer.data(), available_bytes, &bytes_read)) {
            throw std::runtime_error(format_windows_error("Failed while reading response body"));
        }
        response_body.append(buffer.data(), buffer.data() + bytes_read);
    }

    if (status_code != 200) {
        std::string error_message;
        try {
            error_message = ollama_json::extract_top_level_json_string_field(response_body, "error");
        } catch (...) {
        }

        std::ostringstream output;
        output << "Ollama request failed with " << query_status_text(status_code);
        if (!error_message.empty()) {
            output << ": " << error_message;
        } else if (!response_body.empty()) {
            output << ": " << response_body;
        }
        throw std::runtime_error(output.str());
    }

    return response_body;
}

}  // namespace

WindowsOllamaModelClient::WindowsOllamaModelClient(std::string model,
                                                   OllamaConnectionConfig config)
    : model_(std::move(model)),
      config_(std::move(config)) {}

std::string WindowsOllamaModelClient::generate(const std::string& prompt) const {
    const std::string request_body =
        "{\"model\":\"" + ollama_json::escape_json_string(model_) +
        "\",\"prompt\":\"" + ollama_json::escape_json_string(prompt) +
        "\",\"stream\":false}";

    const std::string raw_response = post_json_to_ollama(request_body, config_);

    const std::string error =
        ollama_json::extract_top_level_json_string_field(raw_response, "error");
    if (!error.empty()) {
        throw std::runtime_error(error);
    }

    const std::string response =
        ollama_json::extract_top_level_json_string_field(raw_response, "response");
    if (response.empty()) {
        throw std::runtime_error("Missing response field from Ollama");
    }
    return response;
}

const std::string& WindowsOllamaModelClient::model() const {
    return model_;
}
