#include "api_server.h"

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
#ifndef SOMAXCONN
#define SOMAXCONN 128
#endif
#define closesocket(s) close(s)
using BOOL = int;
#endif

#include "../agent/agent.h"

using json = nlohmann::json;

namespace {

struct ApiRequest {
    std::string method;
    std::string path;
    std::unordered_map<std::string, std::string> headers;
    std::string body;
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
    WinSockSession() {}
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

    SOCKET get() const { return socket_; }
    bool valid() const { return socket_ != static_cast<SOCKET>(INVALID_SOCKET); }

private:
    SOCKET socket_;
};

std::string trim_copy(const std::string& value) {
    const auto begin = value.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) return "";
    const auto end = value.find_last_not_of(" \t\r\n");
    return value.substr(begin, end - begin + 1);
}

std::string to_lower_copy(const std::string& value) {
    std::string lowered = value;
    std::transform(lowered.begin(), lowered.end(), lowered.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return lowered;
}

bool send_all(SOCKET socket, const std::string& data) {
    std::size_t sent = 0;
    while (sent < data.size()) {
        const int result = send(socket, data.data() + sent,
                                static_cast<int>(data.size() - sent), 0);
        if (result <= 0) return false;
        sent += static_cast<std::size_t>(result);
    }
    return true;
}

void send_response(SOCKET socket, int status_code, const std::string& status_text,
                   const std::string& content_type, const std::string& body) {
    std::ostringstream response;
    response << "HTTP/1.1 " << status_code << " " << status_text << "\r\n";
    response << "Content-Type: " << content_type << "\r\n";
    response << "Content-Length: " << body.size() << "\r\n";
    response << "Access-Control-Allow-Origin: *\r\n";
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
        : status_code == 429 ? "Too Many Requests"
        : "Internal Server Error";
    send_response(socket, status_code, status_text, "application/json; charset=utf-8", body);
}

std::size_t content_length_from_headers(
    const std::unordered_map<std::string, std::string>& headers) {
    const auto it = headers.find("content-length");
    if (it == headers.end()) return 0;
    try {
        return static_cast<std::size_t>(std::stoull(it->second));
    } catch (const std::exception&) {
        throw std::runtime_error("Invalid Content-Length");
    }
}

ApiRequest read_request(SOCKET socket) {
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
    ApiRequest request;
    std::string version;
    request_line_stream >> request.method >> request.path >> version;
    if (request.method.empty() || request.path.empty()) {
        throw std::runtime_error("Invalid request line");
    }

    std::string line;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const std::size_t colon = line.find(':');
        if (colon == std::string::npos) continue;
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
    const json parsed = json::parse(body);
    if (!parsed.contains(key) || !parsed[key].is_string()) {
        throw std::runtime_error("Missing JSON field: " + key);
    }
    return parsed[key].get<std::string>();
}

std::string shorten_trace_detail(const std::string& value) {
    constexpr std::size_t kMaxDetailLength = 320;
    if (value.size() <= kMaxDetailLength) return value;
    return value.substr(0, kMaxDetailLength) + "...";
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

// Extract tool name from path like /api/tools/read_file
std::string extract_tool_name(const std::string& path) {
    const std::string prefix = "/api/tools/";
    if (path.size() <= prefix.size()) return "";
    return path.substr(prefix.size());
}

}  // namespace

ApiServer::ApiServer(std::filesystem::path workspace_root,
                     Agent& agent,
                     unsigned short port)
    : workspace_root_(std::move(workspace_root)),
      agent_(agent),
      port_(port),
      thread_pool_(4),
      rate_limiter_(10.0) {
    agent_.set_trace_observer([this](const std::vector<ExecutionStep>& trace) {
        std::lock_guard<std::mutex> lock(state_mutex_);
        last_trace_snapshot_ = trace;
    });
}

int ApiServer::run(std::ostream& output) {
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
        throw std::runtime_error("Failed to bind API server to 127.0.0.1:" +
                                 std::to_string(port_));
    }
    if (listen(listener.get(), SOMAXCONN) == SOCKET_ERROR) {
        throw std::runtime_error("Failed to listen for API connections");
    }

    output << "API server: http://127.0.0.1:" << port_ << "/\n";
    output << "Press Ctrl+C to stop the server.\n";

    while (true) {
        SocketHandle client(accept(listener.get(), nullptr, nullptr));
        if (!client.valid()) continue;

        thread_pool_.submit([this, client_socket = std::make_shared<SocketHandle>(std::move(client))]() {
            try {
                const ApiRequest request = read_request(client_socket->get());

                // Rate limit check
                if (!rate_limiter_.allow()) {
                    send_json(client_socket->get(), 429,
                              json{{"ok", false}, {"error", "Too many requests"}}.dump());
                    return;
                }

                // === Static file serving (React UI) ===
                if (request.method == "GET" && request.path.rfind("/api/", 0) != 0) {
                    // Serve static files from web-dist/ or bolt-ui web-dist
                    std::string file_path = request.path;
                    if (file_path == "/") file_path = "/index.html";

                    // Try multiple asset directories
                    std::vector<std::filesystem::path> search_dirs = {
                        workspace_root_ / "web-dist",
                        workspace_root_ / "bolt-ui" / "web-dist",
                    };
                    // Also check relative to executable
                    auto exe_path = std::filesystem::path("/usr/local/share/bolt/web-dist");
                    if (std::filesystem::exists(exe_path)) search_dirs.push_back(exe_path);

                    bool served = false;
                    for (const auto& dir : search_dirs) {
                        auto full_path = dir / file_path.substr(1);
                        if (std::filesystem::exists(full_path) && std::filesystem::is_regular_file(full_path)) {
                            // Read file
                            std::ifstream file(full_path, std::ios::binary);
                            std::string content((std::istreambuf_iterator<char>(file)),
                                                 std::istreambuf_iterator<char>());

                            // Detect content type
                            std::string ct = "application/octet-stream";
                            std::string ext = full_path.extension().string();
                            if (ext == ".html") ct = "text/html; charset=utf-8";
                            else if (ext == ".css") ct = "text/css; charset=utf-8";
                            else if (ext == ".js") ct = "application/javascript; charset=utf-8";
                            else if (ext == ".json") ct = "application/json";
                            else if (ext == ".svg") ct = "image/svg+xml";
                            else if (ext == ".png") ct = "image/png";
                            else if (ext == ".ico") ct = "image/x-icon";

                            // Add CORS headers for dev mode
                            std::ostringstream resp;
                            resp << "HTTP/1.1 200 OK\r\n";
                            resp << "Content-Type: " << ct << "\r\n";
                            resp << "Content-Length: " << content.size() << "\r\n";
                            resp << "Access-Control-Allow-Origin: *\r\n";
                            resp << "Cache-Control: public, max-age=3600\r\n";
                            resp << "Connection: close\r\n\r\n";
                            resp << content;
                            send_all(client_socket->get(), resp.str());
                            served = true;
                            break;
                        }
                    }

                    if (served) return;

                    // SPA fallback: serve index.html for unknown routes
                    for (const auto& dir : search_dirs) {
                        auto index = dir / "index.html";
                        if (std::filesystem::exists(index)) {
                            std::ifstream file(index, std::ios::binary);
                            std::string content((std::istreambuf_iterator<char>(file)),
                                                 std::istreambuf_iterator<char>());
                            send_response(client_socket->get(), 200, "OK",
                                         "text/html; charset=utf-8", content);
                            served = true;
                            break;
                        }
                    }
                    if (served) return;
                }

                // CORS preflight
                if (request.method == "OPTIONS") {
                    std::ostringstream resp;
                    resp << "HTTP/1.1 204 No Content\r\n";
                    resp << "Access-Control-Allow-Origin: *\r\n";
                    resp << "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n";
                    resp << "Access-Control-Allow-Headers: Content-Type\r\n";
                    resp << "Access-Control-Max-Age: 86400\r\n";
                    resp << "Connection: close\r\n\r\n";
                    send_all(client_socket->get(), resp.str());
                    return;
                }

                // === API Routes ===

                // GET /api/status — model, token counts, session info
                if (request.method == "GET" && request.path == "/api/status") {
                    json result = {
                        {"ok", true},
                        {"model", agent_.model()},
                        {"debug", agent_.debug_enabled()},
                        {"busy", agent_busy_.load()},
                        {"file_count", static_cast<int>(agent_.file_index().file_count())}
                    };
                    send_json(client_socket->get(), 200, result.dump());
                    return;
                }

                // GET /api/tools — list all tools with schemas
                if (request.method == "GET" && request.path == "/api/tools") {
                    json tools_array = json::array();
                    auto tool_names = agent_.available_tool_names();
                    for (const auto& name : tool_names) {
                        json tool_entry = {{"name", name}};
                        tools_array.push_back(tool_entry);
                    }
                    send_json(client_socket->get(), 200,
                              json{{"ok", true}, {"tools", tools_array}}.dump());
                    return;
                }

                // POST /api/tools/:name — execute a tool directly
                if (request.method == "POST" && request.path.rfind("/api/tools/", 0) == 0) {
                    std::string tool_name = extract_tool_name(request.path);
                    if (tool_name.empty()) {
                        send_json(client_socket->get(), 400,
                                  json{{"ok", false}, {"error", "Missing tool name"}}.dump());
                        return;
                    }
                    std::string args;
                    try {
                        args = extract_json_string_field(request.body, "args");
                    } catch (...) {
                        args = "";
                    }

                    try {
                        ToolResult result = agent_.run_diagnostic_tool(tool_name, args);
                        send_json(client_socket->get(), 200,
                                  json{{"ok", true},
                                       {"success", result.success},
                                       {"content", result.content}}.dump());
                    } catch (const std::exception& e) {
                        send_json(client_socket->get(), 404,
                                  json{{"ok", false}, {"error", e.what()}}.dump());
                    }
                    return;
                }

                // POST /api/clear — clear history
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

                // POST /api/chat/stream — SSE streaming
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
                    sse_header << "Connection: keep-alive\r\n";
                    sse_header << "Access-Control-Allow-Origin: *\r\n\r\n";
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
                                    std::string escaped = json(token).dump();
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

                // POST /api/chat — synchronous chat
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

                    auto usage = agent_.last_token_usage();
                    json result = {
                        {"ok", true},
                        {"reply", reply},
                        {"usage", {
                            {"input_tokens", usage.input_tokens},
                            {"output_tokens", usage.output_tokens}
                        }},
                        {"trace", execution_trace_json(trace_snapshot)}
                    };
                    send_json(client_socket->get(), 200, result.dump());
                    return;
                }

                send_json(client_socket->get(), 404,
                          json{{"ok", false}, {"error", "Route not found"}}.dump());
            } catch (const std::exception& error) {
                send_json(client_socket->get(), 500,
                          json{{"ok", false}, {"error", error.what()}}.dump());
            }
        });
    }
}
