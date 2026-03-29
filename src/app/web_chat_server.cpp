#include "web_chat_server.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>

#include "../agent/agent.h"
#include "agent_status.h"
#include "self_check_runner.h"
#include "web_approval_provider.h"

namespace {

struct HttpRequest {
    std::string method;
    std::string path;
    std::unordered_map<std::string, std::string> headers;
    std::string body;
};

class WinSockSession {
public:
    WinSockSession() {
        WSADATA data{};
        const int result = WSAStartup(MAKEWORD(2, 2), &data);
        if (result != 0) {
            throw std::runtime_error("WSAStartup failed");
        }
    }

    ~WinSockSession() {
        WSACleanup();
    }
};

class SocketHandle {
public:
    explicit SocketHandle(SOCKET socket = INVALID_SOCKET) : socket_(socket) {}

    ~SocketHandle() {
        if (valid()) {
            closesocket(socket_);
        }
    }

    SocketHandle(const SocketHandle&) = delete;
    SocketHandle& operator=(const SocketHandle&) = delete;

    SocketHandle(SocketHandle&& other) noexcept : socket_(other.socket_) {
        other.socket_ = INVALID_SOCKET;
    }

    SocketHandle& operator=(SocketHandle&& other) noexcept {
        if (this != &other) {
            if (valid()) {
                closesocket(socket_);
            }
            socket_ = other.socket_;
            other.socket_ = INVALID_SOCKET;
        }
        return *this;
    }

    SOCKET get() const {
        return socket_;
    }

    bool valid() const {
        return socket_ != INVALID_SOCKET;
    }

private:
    SOCKET socket_;
};

std::string trim_copy(const std::string& value) {
    const auto begin = value.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) {
        return "";
    }
    const auto end = value.find_last_not_of(" \t\r\n");
    return value.substr(begin, end - begin + 1);
}

std::string to_lower_copy(const std::string& value) {
    std::string lowered = value;
    std::transform(lowered.begin(), lowered.end(), lowered.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return lowered;
}

std::string escape_json_string(const std::string& value) {
    std::string escaped;
    escaped.reserve(value.size());
    for (const char ch : value) {
        switch (ch) {
            case '\\':
                escaped += "\\\\";
                break;
            case '"':
                escaped += "\\\"";
                break;
            case '\n':
                escaped += "\\n";
                break;
            case '\r':
                escaped += "\\r";
                break;
            case '\t':
                escaped += "\\t";
                break;
            default:
                escaped += ch;
                break;
        }
    }
    return escaped;
}

std::string json_string_field(const std::string& key, const std::string& value) {
    return "\"" + key + "\":\"" + escape_json_string(value) + "\"";
}

std::string json_bool_field(const std::string& key, bool value) {
    return "\"" + key + "\":" + std::string(value ? "true" : "false");
}

std::string json_number_field(const std::string& key, std::size_t value) {
    return "\"" + key + "\":" + std::to_string(value);
}

std::string read_text_file(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("Failed to open asset: " + path.string());
    }
    return std::string((std::istreambuf_iterator<char>(input)),
                       std::istreambuf_iterator<char>());
}

std::string mime_type_for_path(const std::string& path) {
    if (path.size() >= 4 && path.substr(path.size() - 4) == ".css") {
        return "text/css; charset=utf-8";
    }
    if (path.size() >= 3 && path.substr(path.size() - 3) == ".js") {
        return "application/javascript; charset=utf-8";
    }
    return "text/html; charset=utf-8";
}

bool send_all(SOCKET socket, const std::string& data) {
    std::size_t sent = 0;
    while (sent < data.size()) {
        const int result = send(socket, data.data() + sent,
                                static_cast<int>(data.size() - sent), 0);
        if (result <= 0) {
            return false;
        }
        sent += static_cast<std::size_t>(result);
    }
    return true;
}

void send_response(SOCKET socket,
                   int status_code,
                   const std::string& status_text,
                   const std::string& content_type,
                   const std::string& body) {
    std::ostringstream response;
    response << "HTTP/1.1 " << status_code << " " << status_text << "\r\n";
    response << "Content-Type: " << content_type << "\r\n";
    response << "Content-Length: " << body.size() << "\r\n";
    response << "Cache-Control: no-store\r\n";
    response << "Connection: close\r\n\r\n";
    response << body;
    (void)send_all(socket, response.str());
}

void send_json(SOCKET socket, int status_code, const std::string& body) {
    const std::string status_text =
        status_code == 200 ? "OK"
        : status_code == 400 ? "Bad Request"
        : status_code == 404 ? "Not Found"
        : status_code == 409 ? "Conflict"
        : "Internal Server Error";
    send_response(socket, status_code, status_text, "application/json; charset=utf-8", body);
}

std::size_t content_length_from_headers(
    const std::unordered_map<std::string, std::string>& headers) {
    const auto it = headers.find("content-length");
    if (it == headers.end()) {
        return 0;
    }
    try {
        return static_cast<std::size_t>(std::stoull(it->second));
    } catch (const std::exception&) {
        throw std::runtime_error("Invalid Content-Length");
    }
}

HttpRequest read_request(SOCKET socket) {
    std::string raw;
    std::array<char, 4096> buffer{};
    while (raw.find("\r\n\r\n") == std::string::npos) {
        const int bytes = recv(socket, buffer.data(), static_cast<int>(buffer.size()), 0);
        if (bytes <= 0) {
            throw std::runtime_error("Failed to read HTTP headers");
        }
        raw.append(buffer.data(), static_cast<std::size_t>(bytes));
        if (raw.size() > 1024 * 1024) {
            throw std::runtime_error("HTTP request is too large");
        }
    }

    const std::size_t header_end = raw.find("\r\n\r\n");
    const std::string header_block = raw.substr(0, header_end);
    std::string body = raw.substr(header_end + 4);

    std::istringstream input(header_block);
    std::string request_line;
    if (!std::getline(input, request_line)) {
        throw std::runtime_error("Missing request line");
    }
    if (!request_line.empty() && request_line.back() == '\r') {
        request_line.pop_back();
    }

    std::istringstream request_line_stream(request_line);
    HttpRequest request;
    std::string version;
    request_line_stream >> request.method >> request.path >> version;
    if (request.method.empty() || request.path.empty()) {
        throw std::runtime_error("Invalid request line");
    }

    std::string line;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        const std::size_t colon = line.find(':');
        if (colon == std::string::npos) {
            continue;
        }
        request.headers[to_lower_copy(trim_copy(line.substr(0, colon)))] =
            trim_copy(line.substr(colon + 1));
    }

    const std::size_t content_length = content_length_from_headers(request.headers);
    while (body.size() < content_length) {
        const int bytes = recv(socket, buffer.data(), static_cast<int>(buffer.size()), 0);
        if (bytes <= 0) {
            throw std::runtime_error("Failed to read HTTP body");
        }
        body.append(buffer.data(), static_cast<std::size_t>(bytes));
    }
    request.body = body.substr(0, content_length);
    return request;
}

std::string extract_json_string_field(const std::string& body, const std::string& key) {
    const std::string pattern = "\"" + key + "\"";
    const std::size_t key_position = body.find(pattern);
    if (key_position == std::string::npos) {
        throw std::runtime_error("Missing JSON field: " + key);
    }

    const std::size_t colon = body.find(':', key_position + pattern.size());
    if (colon == std::string::npos) {
        throw std::runtime_error("Invalid JSON field: " + key);
    }

    std::size_t quote = body.find('"', colon + 1);
    if (quote == std::string::npos) {
        throw std::runtime_error("JSON string field is missing opening quote: " + key);
    }

    std::string value;
    ++quote;
    bool escaped = false;
    for (std::size_t i = quote; i < body.size(); ++i) {
        const char ch = body[i];
        if (escaped) {
            switch (ch) {
                case 'n':
                    value += '\n';
                    break;
                case 'r':
                    value += '\r';
                    break;
                case 't':
                    value += '\t';
                    break;
                case '\\':
                case '"':
                case '/':
                    value += ch;
                    break;
                default:
                    value += ch;
                    break;
            }
            escaped = false;
            continue;
        }
        if (ch == '\\') {
            escaped = true;
            continue;
        }
        if (ch == '"') {
            return value;
        }
        value += ch;
    }

    throw std::runtime_error("Unterminated JSON string field: " + key);
}

bool extract_json_bool_field(const std::string& body, const std::string& key) {
    const std::string pattern = "\"" + key + "\"";
    const std::size_t key_position = body.find(pattern);
    if (key_position == std::string::npos) {
        throw std::runtime_error("Missing JSON field: " + key);
    }

    const std::size_t colon = body.find(':', key_position + pattern.size());
    if (colon == std::string::npos) {
        throw std::runtime_error("Invalid JSON field: " + key);
    }

    const std::string value = to_lower_copy(trim_copy(body.substr(colon + 1)));
    if (value.rfind("true", 0) == 0) {
        return true;
    }
    if (value.rfind("false", 0) == 0) {
        return false;
    }
    throw std::runtime_error("Invalid boolean field: " + key);
}

std::string approval_snapshot_json(const WebApprovalSnapshot& snapshot, bool busy) {
    std::ostringstream output;
    output << "{";
    output << json_bool_field("busy", busy) << ",";
    output << json_bool_field("has_pending_approval", snapshot.has_pending_request);
    if (snapshot.has_pending_request) {
        output << ",\"approval\":{"
               << json_string_field("tool_name", snapshot.request.tool_name) << ","
               << json_string_field("reason", snapshot.request.reason) << ","
               << json_string_field("risk", snapshot.request.risk) << ","
               << json_string_field("preview_summary", snapshot.request.preview_summary) << ","
               << json_string_field("preview_details", snapshot.request.preview_details)
               << "}";
    }
    output << "}";
    return output.str();
}

std::string shorten_trace_detail(const std::string& value) {
    constexpr std::size_t kMaxDetailLength = 320;
    if (value.size() <= kMaxDetailLength) {
        return value;
    }
    return value.substr(0, kMaxDetailLength) + "...";
}

std::string execution_trace_json(const std::vector<ExecutionStep>& trace) {
    std::ostringstream output;
    output << "[";
    for (std::size_t i = 0; i < trace.size(); ++i) {
        if (i > 0) {
            output << ",";
        }

        const ExecutionStep& step = trace[i];
        output << "{"
               << json_number_field("index", step.index) << ","
               << json_string_field("tool_name", step.tool_name) << ","
               << json_string_field("reason", step.reason) << ","
               << json_string_field("risk", step.risk) << ","
               << json_string_field("status", execution_step_status_name(step.status)) << ","
               << json_string_field("detail", shorten_trace_detail(step.detail))
               << "}";
    }
    output << "]";
    return output.str();
}

std::string capability_state_json(const CapabilityState& capability) {
    std::ostringstream output;
    output << "{"
           << json_string_field("name", capability.name) << ","
           << json_string_field("label", capability.label) << ","
           << json_bool_field("implemented", capability.implemented) << ","
           << json_bool_field("ready", capability.ready) << ","
           << json_bool_field("verified", capability.verified) << ","
           << json_string_field("level", capability.level) << ","
           << json_string_field("detail", capability.detail) << ","
           << json_string_field("last_checked_at", capability.last_checked_at)
           << "}";
    return output.str();
}

std::string capabilities_json(const std::vector<CapabilityState>& capabilities) {
    std::ostringstream output;
    output << "[";
    for (std::size_t i = 0; i < capabilities.size(); ++i) {
        if (i > 0) {
            output << ",";
        }
        output << capability_state_json(capabilities[i]);
    }
    output << "]";
    return output.str();
}

HealthSnapshot build_health_snapshot(const Agent& agent,
                                     bool busy,
                                     bool approval_pending,
                                     const std::vector<CapabilityState>& capabilities,
                                     const std::string& last_self_check_at) {
    HealthSnapshot snapshot;
    snapshot.model = agent.model();
    snapshot.busy = busy;
    snapshot.approval_pending = approval_pending;
    snapshot.last_self_check_at = last_self_check_at;

    for (const CapabilityState& capability : capabilities) {
        if (capability.implemented) {
            ++snapshot.implemented_count;
        }
        if (capability.verified) {
            ++snapshot.verified_count;
        }
        if (capability.level == "degraded" || capability.level == "unavailable") {
            ++snapshot.degraded_count;
        }
    }

    if (snapshot.degraded_count > 0) {
        snapshot.overall = "degraded";
    } else if (snapshot.last_self_check_at.empty()) {
        snapshot.overall = "untested";
    } else {
        snapshot.overall = "ok";
    }
    return snapshot;
}

std::string health_json(const HealthSnapshot& health) {
    std::ostringstream output;
    output << "{"
           << json_string_field("overall", health.overall) << ","
           << json_string_field("model", health.model) << ","
           << json_bool_field("busy", health.busy) << ","
           << json_bool_field("approval_pending", health.approval_pending) << ","
           << json_number_field("implemented_count", health.implemented_count) << ","
           << json_number_field("verified_count", health.verified_count) << ","
           << json_number_field("degraded_count", health.degraded_count) << ","
           << json_string_field("last_self_check_at", health.last_self_check_at)
           << "}";
    return output.str();
}

std::string state_json(const WebApprovalSnapshot& snapshot,
                       bool busy,
                       const std::vector<ExecutionStep>& trace) {
    std::ostringstream output;
    output << "{";
    output << json_bool_field("busy", busy) << ",";
    output << json_bool_field("has_pending_approval", snapshot.has_pending_request) << ",";
    output << "\"last_trace\":" << execution_trace_json(trace);
    if (snapshot.has_pending_request) {
        output << ",\"approval\":{"
               << json_string_field("tool_name", snapshot.request.tool_name) << ","
               << json_string_field("reason", snapshot.request.reason) << ","
               << json_string_field("risk", snapshot.request.risk) << ","
               << json_string_field("preview_summary", snapshot.request.preview_summary) << ","
               << json_string_field("preview_details", snapshot.request.preview_details)
               << "}";
    }
    output << "}";
    return output.str();
}

std::string info_json(const Agent& agent, unsigned short port, bool busy) {
    std::ostringstream output;
    output << "{"
           << json_string_field("model", agent.model()) << ","
           << json_bool_field("debug", agent.debug_enabled()) << ","
           << "\"port\":" << port << ","
           << json_bool_field("busy", busy)
           << "}";
    return output.str();
}

}  // namespace

WebChatServer::WebChatServer(std::filesystem::path workspace_root,
                             Agent& agent,
                             std::shared_ptr<WebApprovalProvider> approval_provider,
                             unsigned short port)
    : workspace_root_(std::move(workspace_root)),
      agent_(agent),
      approval_provider_(std::move(approval_provider)),
      port_(port) {
    if (approval_provider_ == nullptr) {
        throw std::invalid_argument("WebChatServer requires a web approval provider");
    }

    {
        SelfCheckRunner self_check_runner(agent_, workspace_root_);
        std::lock_guard<std::mutex> lock(state_mutex_);
        last_trace_snapshot_ = agent_.last_execution_trace();
        capability_snapshot_ = self_check_runner.build_initial_snapshot();
        last_self_check_at_ = self_check_runner.last_checked_at();
    }
    agent_.set_trace_observer([this](const std::vector<ExecutionStep>& trace) {
        std::lock_guard<std::mutex> lock(state_mutex_);
        last_trace_snapshot_ = trace;
    });
}

int WebChatServer::run(std::ostream& output) {
    WinSockSession winsock;

    SocketHandle listener(socket(AF_INET, SOCK_STREAM, IPPROTO_TCP));
    if (!listener.valid()) {
        throw std::runtime_error("Failed to create listening socket");
    }

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port_);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    const BOOL reuse = 1;
    setsockopt(listener.get(), SOL_SOCKET, SO_REUSEADDR,
               reinterpret_cast<const char*>(&reuse), sizeof(reuse));

    if (bind(listener.get(), reinterpret_cast<sockaddr*>(&address), sizeof(address)) == SOCKET_ERROR) {
        throw std::runtime_error("Failed to bind web-chat server to 127.0.0.1:" +
                                 std::to_string(port_));
    }
    if (listen(listener.get(), SOMAXCONN) == SOCKET_ERROR) {
        throw std::runtime_error("Failed to listen for web-chat connections");
    }

    output << "Web chat UI: http://127.0.0.1:" << port_ << "/\n";
    output << "Press Ctrl+C to stop the server.\n";

    while (true) {
        SocketHandle client(accept(listener.get(), nullptr, nullptr));
        if (!client.valid()) {
            continue;
        }

        std::thread([this, client_socket = std::move(client)]() mutable {
            try {
                const HttpRequest request = read_request(client_socket.get());
                if (request.method == "GET" &&
                    (request.path == "/" || request.path == "/index.html" ||
                     request.path == "/app.js" || request.path == "/styles.css")) {
                    const std::string relative_path =
                        request.path == "/" ? "web/index.html" : "web" + request.path;
                    const std::filesystem::path asset_path = workspace_root_ / relative_path;
                    const std::string body = read_text_file(asset_path);
                    send_response(client_socket.get(), 200, "OK",
                                  mime_type_for_path(relative_path), body);
                    return;
                }

                if (request.method == "GET" && request.path == "/api/info") {
                    send_json(client_socket.get(), 200,
                              info_json(agent_, port_, agent_busy_.load()));
                    return;
                }

                if (request.method == "GET" && request.path == "/api/state") {
                    std::vector<ExecutionStep> trace_snapshot;
                    {
                        std::lock_guard<std::mutex> lock(state_mutex_);
                        trace_snapshot = last_trace_snapshot_;
                    }
                    send_json(client_socket.get(), 200,
                              state_json(approval_provider_->snapshot(), agent_busy_.load(),
                                         trace_snapshot));
                    return;
                }

                if (request.method == "GET" && request.path == "/api/capabilities") {
                    std::vector<CapabilityState> capability_snapshot;
                    {
                        std::lock_guard<std::mutex> lock(state_mutex_);
                        capability_snapshot = capability_snapshot_;
                    }
                    send_json(client_socket.get(), 200,
                              std::string("{\"capabilities\":") +
                                  capabilities_json(capability_snapshot) + "}");
                    return;
                }

                if (request.method == "GET" && request.path == "/api/health") {
                    std::vector<CapabilityState> capability_snapshot;
                    std::string last_self_check_at;
                    {
                        std::lock_guard<std::mutex> lock(state_mutex_);
                        capability_snapshot = capability_snapshot_;
                        last_self_check_at = last_self_check_at_;
                    }
                    const WebApprovalSnapshot approval_snapshot = approval_provider_->snapshot();
                    const HealthSnapshot health = build_health_snapshot(
                        agent_, agent_busy_.load(), approval_snapshot.has_pending_request,
                        capability_snapshot, last_self_check_at);
                    send_json(client_socket.get(), 200, health_json(health));
                    return;
                }

                if (request.method == "POST" && request.path == "/api/clear") {
                    if (agent_busy_.load()) {
                        send_json(client_socket.get(), 409,
                                  "{\"ok\":false,\"error\":\"Agent is busy\"}");
                        return;
                    }
                    std::lock_guard<std::mutex> lock(agent_mutex_);
                    agent_.clear_history();
                    {
                        std::lock_guard<std::mutex> state_lock(state_mutex_);
                        last_trace_snapshot_.clear();
                    }
                    send_json(client_socket.get(), 200, "{\"ok\":true}");
                    return;
                }

                if (request.method == "POST" && request.path == "/api/self-check") {
                    bool expected = false;
                    if (!agent_busy_.compare_exchange_strong(expected, true)) {
                        send_json(client_socket.get(), 409,
                                  "{\"ok\":false,\"error\":\"Agent is busy\"}");
                        return;
                    }

                    std::vector<CapabilityState> capability_snapshot;
                    std::string last_self_check_at;
                    std::string error;
                    {
                        std::lock_guard<std::mutex> lock(agent_mutex_);
                        try {
                            SelfCheckRunner self_check_runner(agent_, workspace_root_);
                            capability_snapshot = self_check_runner.run();
                            last_self_check_at = self_check_runner.last_checked_at();
                        } catch (const std::exception& exception) {
                            error = exception.what();
                        }
                    }
                    agent_busy_.store(false);

                    if (!error.empty()) {
                        send_json(client_socket.get(), 500,
                                  std::string("{\"ok\":false,\"error\":\"") +
                                      escape_json_string(error) + "\"}");
                        return;
                    }

                    {
                        std::lock_guard<std::mutex> lock(state_mutex_);
                        capability_snapshot_ = capability_snapshot;
                        last_self_check_at_ = last_self_check_at;
                    }

                    const WebApprovalSnapshot approval_snapshot = approval_provider_->snapshot();
                    const HealthSnapshot health = build_health_snapshot(
                        agent_, agent_busy_.load(), approval_snapshot.has_pending_request,
                        capability_snapshot, last_self_check_at);
                    send_json(client_socket.get(), 200,
                              std::string("{\"ok\":true,\"health\":") + health_json(health) +
                                  ",\"capabilities\":" + capabilities_json(capability_snapshot) +
                                  "}");
                    return;
                }

                if (request.method == "POST" && request.path == "/api/approval") {
                    const bool approved = extract_json_bool_field(request.body, "approved");
                    if (!approval_provider_->resolve(approved)) {
                        send_json(client_socket.get(), 409,
                                  "{\"ok\":false,\"error\":\"No pending approval\"}");
                        return;
                    }
                    send_json(client_socket.get(), 200, "{\"ok\":true}");
                    return;
                }

                // SSE streaming chat endpoint — real-time token passthrough
                if (request.method == "POST" && request.path == "/api/chat/stream") {
                    const std::string message = extract_json_string_field(request.body, "message");
                    if (trim_copy(message).empty()) {
                        send_json(client_socket.get(), 400,
                                  "{\"ok\":false,\"error\":\"Message is empty\"}");
                        return;
                    }

                    bool expected = false;
                    if (!agent_busy_.compare_exchange_strong(expected, true)) {
                        send_json(client_socket.get(), 409,
                                  "{\"ok\":false,\"error\":\"Agent is busy\"}");
                        return;
                    }

                    // Send SSE headers
                    std::ostringstream sse_header;
                    sse_header << "HTTP/1.1 200 OK\r\n";
                    sse_header << "Content-Type: text/event-stream; charset=utf-8\r\n";
                    sse_header << "Cache-Control: no-cache\r\n";
                    sse_header << "Connection: keep-alive\r\n";
                    sse_header << "Access-Control-Allow-Origin: *\r\n\r\n";
                    if (!send_all(client_socket.get(), sse_header.str())) {
                        agent_busy_.store(false);
                        return;
                    }

                    SOCKET raw_socket = client_socket.get();
                    std::string reply;
                    std::string error;
                    std::vector<ExecutionStep> trace_snapshot;
                    {
                        std::lock_guard<std::mutex> lock(agent_mutex_);
                        try {
                            reply = agent_.run_turn_streaming(message,
                                [raw_socket](const std::string& token) {
                                    // SSE passthrough: forward each token as an SSE event
                                    std::string sse_event = "data: " + escape_json_string(token) + "\n\n";
                                    (void)send_all(raw_socket, sse_event);
                                });
                            trace_snapshot = agent_.last_execution_trace();
                        } catch (const std::exception& exception) {
                            error = exception.what();
                            trace_snapshot = agent_.last_execution_trace();
                        }
                    }
                    {
                        std::lock_guard<std::mutex> state_lock(state_mutex_);
                        last_trace_snapshot_ = trace_snapshot;
                    }
                    agent_busy_.store(false);

                    // Send final event with trace and status
                    if (!error.empty()) {
                        std::string err_event = "event: error\ndata: " +
                                                escape_json_string(error) + "\n\n";
                        (void)send_all(raw_socket, err_event);
                    } else {
                        std::string done_event = "event: done\ndata: " +
                            execution_trace_json(trace_snapshot) + "\n\n";
                        (void)send_all(raw_socket, done_event);
                    }
                    return;
                }

                // Original synchronous chat endpoint (kept for backward compatibility)
                if (request.method == "POST" && request.path == "/api/chat") {
                    const std::string message = extract_json_string_field(request.body, "message");
                    if (trim_copy(message).empty()) {
                        send_json(client_socket.get(), 400,
                                  "{\"ok\":false,\"error\":\"Message is empty\"}");
                        return;
                    }

                    bool expected = false;
                    if (!agent_busy_.compare_exchange_strong(expected, true)) {
                        send_json(client_socket.get(), 409,
                                  "{\"ok\":false,\"error\":\"Agent is busy\"}");
                        return;
                    }

                    std::string reply;
                    std::string error;
                    std::vector<ExecutionStep> trace_snapshot;
                    {
                        std::lock_guard<std::mutex> lock(agent_mutex_);
                        try {
                            reply = agent_.run_turn(message);
                            trace_snapshot = agent_.last_execution_trace();
                        } catch (const std::exception& exception) {
                            error = exception.what();
                            trace_snapshot = agent_.last_execution_trace();
                        }
                    }
                    {
                        std::lock_guard<std::mutex> state_lock(state_mutex_);
                        last_trace_snapshot_ = trace_snapshot;
                    }
                    agent_busy_.store(false);

                    if (!error.empty()) {
                        send_json(client_socket.get(), 500,
                                  std::string("{\"ok\":false,\"error\":\"") +
                                      escape_json_string(error) + "\"}");
                        return;
                    }

                    send_json(client_socket.get(), 200,
                              std::string("{\"ok\":true,\"reply\":\"") +
                                  escape_json_string(reply) + "\",\"trace\":" +
                                  execution_trace_json(trace_snapshot) + "}");
                    return;
                }

                send_json(client_socket.get(), 404,
                          "{\"ok\":false,\"error\":\"Route not found\"}");
            } catch (const std::exception& error) {
                send_json(client_socket.get(), 500,
                          std::string("{\"ok\":false,\"error\":\"") +
                              escape_json_string(error.what()) + "\"}");
            }
        }).detach();
    }
}
