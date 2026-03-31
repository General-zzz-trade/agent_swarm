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
#include <unordered_map>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#define SOCKET int
#define INVALID_SOCKET (-1)
#define SOCKET_ERROR (-1)
#define SOMAXCONN 128
#define closesocket(s) close(s)
using BOOL = int;
#endif

#include "../agent/agent.h"
#include "agent_status.h"
#include "http_server_utils.h"
#include "self_check_runner.h"
#include "web_approval_provider.h"

using json = nlohmann::json;
using namespace http_server_utils;

namespace {

struct WebRequest {
    std::string method;
    std::string path;
    std::unordered_map<std::string, std::string> headers;
    std::string body;
};

class HttpError : public std::runtime_error {
public:
    HttpError(int status_code, std::string message)
        : std::runtime_error(std::move(message)), status_code_(status_code) {}

    int status_code() const { return status_code_; }

private:
    int status_code_;
};

#ifdef _WIN32
class WinSockSession {
public:
    WinSockSession() {
        WSADATA data{};
        const int result = WSAStartup(MAKEWORD(2, 2), &data);
        if (result != 0) {
            throw std::runtime_error("WSAStartup failed");
        }
    }
    ~WinSockSession() { WSACleanup(); }
};
#else
class WinSockSession {
public:
    WinSockSession() {}  // No-op on Linux/macOS
    ~WinSockSession() {}
};
#endif

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
        return socket_ != static_cast<SOCKET>(INVALID_SOCKET);
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
    response << "X-Content-Type-Options: nosniff\r\n";
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
        : status_code == 413 ? "Payload Too Large"
        : status_code == 429 ? "Too Many Requests"
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
        const std::size_t content_length = static_cast<std::size_t>(std::stoull(it->second));
        if (content_length > kMaxHttpBodyBytes) {
            throw HttpError(413, "HTTP request body is too large");
        }
        return content_length;
    } catch (const HttpError&) {
        throw;
    } catch (const std::exception&) {
        throw HttpError(400, "Invalid Content-Length");
    }
}

WebRequest read_request(SOCKET socket) {
    std::string raw;
    std::array<char, 4096> buffer{};
    while (raw.find("\r\n\r\n") == std::string::npos) {
        const int bytes = recv(socket, buffer.data(), static_cast<int>(buffer.size()), 0);
        if (bytes <= 0) {
            throw HttpError(400, "Failed to read HTTP headers");
        }
        raw.append(buffer.data(), static_cast<std::size_t>(bytes));
        if (raw.size() > kMaxHttpHeaderBytes) {
            throw HttpError(413, "HTTP request headers are too large");
        }
    }

    const std::size_t header_end = raw.find("\r\n\r\n");
    const std::string header_block = raw.substr(0, header_end);
    std::string body = raw.substr(header_end + 4);

    std::istringstream input(header_block);
    std::string request_line;
    if (!std::getline(input, request_line)) {
        throw HttpError(400, "Missing request line");
    }
    if (!request_line.empty() && request_line.back() == '\r') {
        request_line.pop_back();
    }

    std::istringstream request_line_stream(request_line);
    WebRequest request;
    std::string version;
    std::string request_target;
    request_line_stream >> request.method >> request_target >> version;
    if (request.method.empty() || request_target.empty()) {
        throw HttpError(400, "Invalid request line");
    }
    try {
        request.path = normalize_request_path(request_target);
    } catch (const std::exception& e) {
        throw HttpError(400, e.what());
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
            throw HttpError(400, "Failed to read HTTP body");
        }
        body.append(buffer.data(), static_cast<std::size_t>(bytes));
        if (body.size() > content_length || body.size() > kMaxHttpBodyBytes) {
            throw HttpError(413, "HTTP request body is too large");
        }
    }
    request.body = body.substr(0, content_length);
    return request;
}

std::string extract_json_string_field(const std::string& body, const std::string& key) {
    json parsed;
    try {
        parsed = json::parse(body);
    } catch (const std::exception&) {
        throw HttpError(400, "Invalid JSON body");
    }
    if (!parsed.contains(key) || !parsed[key].is_string()) {
        throw HttpError(400, "Missing JSON field: " + key);
    }
    return parsed[key].get<std::string>();
}

bool extract_json_bool_field(const std::string& body, const std::string& key) {
    json parsed;
    try {
        parsed = json::parse(body);
    } catch (const std::exception&) {
        throw HttpError(400, "Invalid JSON body");
    }
    if (!parsed.contains(key) || !parsed[key].is_boolean()) {
        throw HttpError(400, "Missing JSON field: " + key);
    }
    return parsed[key].get<bool>();
}

std::string shorten_trace_detail(const std::string& value) {
    constexpr std::size_t kMaxDetailLength = 320;
    if (value.size() <= kMaxDetailLength) {
        return value;
    }
    return value.substr(0, kMaxDetailLength) + "...";
}

json approval_request_json(const ApprovalRequest& request) {
    return json{
        {"tool_name", request.tool_name},
        {"reason", request.reason},
        {"risk", request.risk},
        {"preview_summary", request.preview_summary},
        {"preview_details", request.preview_details}
    };
}

json approval_snapshot_json(const WebApprovalSnapshot& snapshot, bool busy) {
    json result = {
        {"busy", busy},
        {"has_pending_approval", snapshot.has_pending_request}
    };
    if (snapshot.has_pending_request) {
        result["approval"] = approval_request_json(snapshot.request);
    }
    return result;
}

json execution_step_json(const ExecutionStep& step) {
    return json{
        {"index", step.index},
        {"tool_name", step.tool_name},
        {"reason", step.reason},
        {"risk", step.risk},
        {"status", execution_step_status_name(step.status)},
        {"detail", shorten_trace_detail(step.detail)}
    };
}

json execution_trace_json(const std::vector<ExecutionStep>& trace) {
    json result = json::array();
    for (const auto& step : trace) {
        result.push_back(execution_step_json(step));
    }
    return result;
}

json capability_state_json(const CapabilityState& capability) {
    return json{
        {"name", capability.name},
        {"label", capability.label},
        {"implemented", capability.implemented},
        {"ready", capability.ready},
        {"verified", capability.verified},
        {"level", capability.level},
        {"detail", capability.detail},
        {"last_checked_at", capability.last_checked_at}
    };
}

json capabilities_json(const std::vector<CapabilityState>& capabilities) {
    json result = json::array();
    for (const auto& cap : capabilities) {
        result.push_back(capability_state_json(cap));
    }
    return result;
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

json health_json(const HealthSnapshot& health) {
    return json{
        {"overall", health.overall},
        {"model", health.model},
        {"busy", health.busy},
        {"approval_pending", health.approval_pending},
        {"implemented_count", health.implemented_count},
        {"verified_count", health.verified_count},
        {"degraded_count", health.degraded_count},
        {"last_self_check_at", health.last_self_check_at}
    };
}

json state_json(const WebApprovalSnapshot& snapshot,
                bool busy,
                const std::vector<ExecutionStep>& trace) {
    json result = {
        {"busy", busy},
        {"has_pending_approval", snapshot.has_pending_request},
        {"last_trace", execution_trace_json(trace)}
    };
    if (snapshot.has_pending_request) {
        result["approval"] = approval_request_json(snapshot.request);
    }
    return result;
}

json info_json(const Agent& agent, unsigned short port, bool busy) {
    return json{
        {"model", agent.model()},
        {"debug", agent.debug_enabled()},
        {"port", port},
        {"busy", busy}
    };
}

}  // namespace

WebChatServer::WebChatServer(std::filesystem::path workspace_root,
                             Agent& agent,
                             std::shared_ptr<WebApprovalProvider> approval_provider,
                             unsigned short port)
    : workspace_root_(std::move(workspace_root)),
      agent_(agent),
      approval_provider_(std::move(approval_provider)),
      port_(port),
      thread_pool_(4),
      rate_limiter_(10.0) {
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

    const int reuse = 1;
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

        thread_pool_.submit([this, client_socket = std::make_shared<SocketHandle>(std::move(client))]() {
            try {
                const WebRequest request = read_request(client_socket->get());

                // Rate limit check
                if (!rate_limiter_.allow()) {
                    send_json(client_socket->get(), 429,
                              json{{"ok", false}, {"error", "Too many requests"}}.dump());
                    return;
                }

                if (request.method == "GET" &&
                    (request.path == "/" || request.path == "/index.html" ||
                     request.path == "/app.js" || request.path == "/styles.css")) {
                    const std::string relative_path =
                        request.path == "/" ? "web/index.html" : "web" + request.path;
                    const std::filesystem::path asset_path = workspace_root_ / relative_path;
                    const std::string body = read_text_file(asset_path);
                    send_response(client_socket->get(), 200, "OK",
                                  mime_type_for_path(relative_path), body);
                    return;
                }

                if (request.method == "GET" && request.path == "/api/info") {
                    send_json(client_socket->get(), 200,
                              info_json(agent_, port_, agent_busy_.load()).dump());
                    return;
                }

                if (request.method == "GET" && request.path == "/api/state") {
                    std::vector<ExecutionStep> trace_snapshot;
                    {
                        std::lock_guard<std::mutex> lock(state_mutex_);
                        trace_snapshot = last_trace_snapshot_;
                    }
                    send_json(client_socket->get(), 200,
                              state_json(approval_provider_->snapshot(), agent_busy_.load(),
                                         trace_snapshot).dump());
                    return;
                }

                if (request.method == "GET" && request.path == "/api/capabilities") {
                    std::vector<CapabilityState> capability_snapshot;
                    {
                        std::lock_guard<std::mutex> lock(state_mutex_);
                        capability_snapshot = capability_snapshot_;
                    }
                    json result = {{"capabilities", capabilities_json(capability_snapshot)}};
                    send_json(client_socket->get(), 200, result.dump());
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
                    send_json(client_socket->get(), 200, health_json(health).dump());
                    return;
                }

                if (request.method == "POST" && request.path == "/api/clear") {
                    if (agent_busy_.load()) {
                        send_json(client_socket->get(), 409,
                                  json{{"ok", false}, {"error", "Agent is busy"}}.dump());
                        return;
                    }
                    std::lock_guard<std::mutex> lock(agent_mutex_);
                    agent_.clear_history();
                    {
                        std::lock_guard<std::mutex> state_lock(state_mutex_);
                        last_trace_snapshot_.clear();
                    }
                    send_json(client_socket->get(), 200, json{{"ok", true}}.dump());
                    return;
                }

                if (request.method == "POST" && request.path == "/api/self-check") {
                    bool expected = false;
                    if (!agent_busy_.compare_exchange_strong(expected, true)) {
                        send_json(client_socket->get(), 409,
                                  json{{"ok", false}, {"error", "Agent is busy"}}.dump());
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
                        send_json(client_socket->get(), 500,
                                  json{{"ok", false}, {"error", error}}.dump());
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
                    json result = {
                        {"ok", true},
                        {"health", health_json(health)},
                        {"capabilities", capabilities_json(capability_snapshot)}
                    };
                    send_json(client_socket->get(), 200, result.dump());
                    return;
                }

                if (request.method == "POST" && request.path == "/api/approval") {
                    const bool approved = extract_json_bool_field(request.body, "approved");
                    if (!approval_provider_->resolve(approved)) {
                        send_json(client_socket->get(), 409,
                                  json{{"ok", false}, {"error", "No pending approval"}}.dump());
                        return;
                    }
                    send_json(client_socket->get(), 200, json{{"ok", true}}.dump());
                    return;
                }

                // SSE streaming chat endpoint -- real-time token passthrough
                if (request.method == "POST" && request.path == "/api/chat/stream") {
                    const std::string message = extract_json_string_field(request.body, "message");
                    if (trim_copy(message).empty()) {
                        send_json(client_socket->get(), 400,
                                  json{{"ok", false}, {"error", "Message is empty"}}.dump());
                        return;
                    }

                    bool expected = false;
                    if (!agent_busy_.compare_exchange_strong(expected, true)) {
                        send_json(client_socket->get(), 409,
                                  json{{"ok", false}, {"error", "Agent is busy"}}.dump());
                        return;
                    }

                    // Send SSE headers
                    std::ostringstream sse_header;
                    sse_header << "HTTP/1.1 200 OK\r\n";
                    sse_header << "Content-Type: text/event-stream; charset=utf-8\r\n";
                    sse_header << "Cache-Control: no-cache\r\n";
                    sse_header << "Connection: keep-alive\r\n\r\n";
                    if (!send_all(client_socket->get(), sse_header.str())) {
                        agent_busy_.store(false);
                        return;
                    }

                    SOCKET raw_socket = client_socket->get();
                    std::string reply;
                    std::string error;
                    std::vector<ExecutionStep> trace_snapshot;
                    {
                        std::lock_guard<std::mutex> lock(agent_mutex_);
                        try {
                            reply = agent_.run_turn_streaming(message,
                                [raw_socket](const std::string& token) {
                                    // SSE passthrough: forward each token as an SSE event
                                    // Use json to properly escape the token for SSE data lines
                                    std::string escaped = json(token).dump();
                                    // Strip surrounding quotes from the dump
                                    std::string sse_event = "data: " +
                                        escaped.substr(1, escaped.size() - 2) + "\n\n";
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
                        std::string escaped = json(error).dump();
                        std::string err_event = "event: error\ndata: " +
                                                escaped.substr(1, escaped.size() - 2) + "\n\n";
                        (void)send_all(raw_socket, err_event);
                    } else {
                        std::string done_event = "event: done\ndata: " +
                            execution_trace_json(trace_snapshot).dump() + "\n\n";
                        (void)send_all(raw_socket, done_event);
                    }
                    return;
                }

                // Original synchronous chat endpoint (kept for backward compatibility)
                if (request.method == "POST" && request.path == "/api/chat") {
                    const std::string message = extract_json_string_field(request.body, "message");
                    if (trim_copy(message).empty()) {
                        send_json(client_socket->get(), 400,
                                  json{{"ok", false}, {"error", "Message is empty"}}.dump());
                        return;
                    }

                    bool expected = false;
                    if (!agent_busy_.compare_exchange_strong(expected, true)) {
                        send_json(client_socket->get(), 409,
                                  json{{"ok", false}, {"error", "Agent is busy"}}.dump());
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
                        send_json(client_socket->get(), 500,
                                  json{{"ok", false}, {"error", error}}.dump());
                        return;
                    }

                    json result = {
                        {"ok", true},
                        {"reply", reply},
                        {"trace", execution_trace_json(trace_snapshot)}
                    };
                    send_json(client_socket->get(), 200, result.dump());
                    return;
                }

                send_json(client_socket->get(), 404,
                          json{{"ok", false}, {"error", "Route not found"}}.dump());
            } catch (const HttpError& error) {
                send_json(client_socket->get(), error.status_code(),
                          json{{"ok", false}, {"error", error.what()}}.dump());
            } catch (const std::exception& error) {
                send_json(client_socket->get(), 500,
                          json{{"ok", false}, {"error", error.what()}}.dump());
            }
        });
    }
}
