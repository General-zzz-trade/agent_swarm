#include "linux_http_transport.h"

#include <array>
#include <arpa/inet.h>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <netdb.h>
#include <sstream>
#include <sys/socket.h>
#include <unistd.h>

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

namespace {

int connect_to_host(const std::string& host, int port, int timeout_ms) {
    struct addrinfo hints{}, *res = nullptr;
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    const std::string port_str = std::to_string(port);
    if (getaddrinfo(host.c_str(), port_str.c_str(), &hints, &res) != 0 || !res) {
        return -1;
    }

    const int fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (fd < 0) {
        freeaddrinfo(res);
        return -1;
    }

    // Set timeout
    struct timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    if (connect(fd, res->ai_addr, res->ai_addrlen) != 0) {
        close(fd);
        freeaddrinfo(res);
        return -1;
    }

    freeaddrinfo(res);
    return fd;
}

bool send_all(int fd, const std::string& data) {
    std::size_t sent = 0;
    while (sent < data.size()) {
        const ssize_t n = ::send(fd, data.data() + sent, data.size() - sent, MSG_NOSIGNAL);
        if (n <= 0) return false;
        sent += static_cast<std::size_t>(n);
    }
    return true;
}

}  // namespace

LinuxHttpTransport::LinuxHttpTransport() = default;

LinuxHttpTransport::~LinuxHttpTransport() {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& [key, fd] : connections_) {
        if (fd >= 0) close(fd);
    }
}

LinuxHttpTransport::ParsedUrl LinuxHttpTransport::parse_url(const std::string& url) {
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
    }

    const auto path_pos = remaining.find('/');
    const std::string host_part = remaining.substr(0, path_pos);
    parsed.path = path_pos != std::string::npos ? remaining.substr(path_pos) : "/";

    const auto colon = host_part.find(':');
    if (colon != std::string::npos) {
        parsed.host = host_part.substr(0, colon);
        parsed.port = std::stoi(host_part.substr(colon + 1));
    } else {
        parsed.host = host_part;
    }

    return parsed;
}

HttpResponse LinuxHttpTransport::send(const HttpRequest& request) {
    return execute_request(request, nullptr);
}

HttpResponse LinuxHttpTransport::send_streaming(
    const HttpRequest& request,
    std::function<bool(const std::string& chunk)> on_chunk) {
    return execute_request(request, &on_chunk);
}

HttpResponse LinuxHttpTransport::execute_request(
    const HttpRequest& request,
    std::function<bool(const std::string& chunk)>* on_chunk) {

    const ParsedUrl parsed = parse_url(request.url);

    if (parsed.secure) {
        // HTTPS: delegate to system curl (avoids OpenSSL compile dependency)
        return execute_via_curl(request, on_chunk);
    }

    const int timeout = request.timeout_ms > 0 ? request.timeout_ms : 60000;
    const int fd = connect_to_host(parsed.host, parsed.port, timeout);
    if (fd < 0) {
        return {0, "", "Failed to connect to " + parsed.host + ":" + std::to_string(parsed.port)};
    }

    // Build HTTP request
    const std::string method = request.method.empty() ? "POST" : request.method;
    std::ostringstream http_req;
    http_req << method << " " << parsed.path << " HTTP/1.1\r\n";
    http_req << "Host: " << parsed.host << "\r\n";
    http_req << "Content-Length: " << request.body.size() << "\r\n";
    for (const auto& [key, value] : request.headers) {
        http_req << key << ": " << value << "\r\n";
    }
    http_req << "Connection: close\r\n\r\n";
    http_req << request.body;

    if (!send_all(fd, http_req.str())) {
        close(fd);
        return {0, "", "Failed to send HTTP request"};
    }

    // Read response
    std::string raw_response;
    char buf[8192];
    while (true) {
        const ssize_t n = recv(fd, buf, sizeof(buf), 0);
        if (n <= 0) break;
        raw_response.append(buf, static_cast<std::size_t>(n));

        // If streaming, forward body chunks after headers are complete
        if (on_chunk) {
            const auto header_end = raw_response.find("\r\n\r\n");
            if (header_end != std::string::npos) {
                const std::string body_so_far = raw_response.substr(header_end + 4);
                if (!body_so_far.empty()) {
                    if (!(*on_chunk)(body_so_far)) {
                        break;
                    }
                    raw_response.resize(header_end + 4);
                }
                // Continue reading body
                while (true) {
                    const ssize_t m = recv(fd, buf, sizeof(buf), 0);
                    if (m <= 0) break;
                    const std::string chunk(buf, static_cast<std::size_t>(m));
                    if (!(*on_chunk)(chunk)) break;
                }
                break;
            }
        }
    }

    close(fd);

    // Parse response
    HttpResponse response;
    const auto header_end = raw_response.find("\r\n\r\n");
    if (header_end == std::string::npos) {
        response.error = "Invalid HTTP response";
        return response;
    }

    // Parse status code from first line
    const auto first_line_end = raw_response.find("\r\n");
    const std::string status_line = raw_response.substr(0, first_line_end);
    // "HTTP/1.1 200 OK"
    const auto space1 = status_line.find(' ');
    if (space1 != std::string::npos) {
        try {
            response.status_code = std::stoi(status_line.substr(space1 + 1));
        } catch (...) {}
    }

    if (!on_chunk) {
        response.body = raw_response.substr(header_end + 4);
    }

    return response;
}

HttpResponse LinuxHttpTransport::execute_via_curl(
    const HttpRequest& request,
    std::function<bool(const std::string& chunk)>* on_chunk) {

    // Build curl command
    const std::string method = request.method.empty() ? "POST" : request.method;
    std::ostringstream cmd;
    cmd << "curl -sS -X " << method;
    cmd << " --max-time " << (request.timeout_ms > 0 ? request.timeout_ms / 1000 : 60);

    // Headers
    for (const auto& [key, value] : request.headers) {
        cmd << " -H '" << key << ": " << value << "'";
    }

    // Body via stdin to avoid shell escaping issues
    if (!request.body.empty()) {
        cmd << " -d @-";
    }

    // Include response headers for status code parsing
    cmd << " -w '\\n__HTTP_STATUS__:%{http_code}'";
    cmd << " '" << request.url << "'";

    // For streaming, use -N (no buffer)
    if (on_chunk) {
        cmd << " --no-buffer";
    }

    FILE* pipe = popen(cmd.str().c_str(), request.body.empty() ? "r" : "r+");
    if (!pipe) {
        return {0, "", "Failed to launch curl for HTTPS request"};
    }

    // Write body to stdin if present
    if (!request.body.empty()) {
        // popen with "r+" isn't portable, use a temp approach:
        // Close this pipe and reopen with body piped through echo
        pclose(pipe);

        // Use a temp file for the body
        const std::string tmp = "/tmp/bolt_req_" + std::to_string(getpid()) + ".json";
        {
            FILE* f = fopen(tmp.c_str(), "w");
            if (f) {
                fwrite(request.body.data(), 1, request.body.size(), f);
                fclose(f);
            }
        }

        // Rebuild command with -d @tmpfile
        std::ostringstream cmd2;
        cmd2 << "curl -sS -X " << method;
        cmd2 << " --max-time " << (request.timeout_ms > 0 ? request.timeout_ms / 1000 : 60);
        for (const auto& [key, value] : request.headers) {
            cmd2 << " -H '" << key << ": " << value << "'";
        }
        cmd2 << " -d @" << tmp;
        cmd2 << " -w '\\n__HTTP_STATUS__:%{http_code}'";
        if (on_chunk) cmd2 << " --no-buffer";
        cmd2 << " '" << request.url << "'";

        pipe = popen(cmd2.str().c_str(), "r");
        if (!pipe) {
            unlink(tmp.c_str());
            return {0, "", "Failed to launch curl"};
        }

        // Read response
        HttpResponse response;
        std::string body;
        std::array<char, 8192> buf;

        while (true) {
            const size_t n = fread(buf.data(), 1, buf.size(), pipe);
            if (n == 0) break;
            const std::string chunk(buf.data(), n);

            if (on_chunk) {
                // Check if this chunk contains the status line
                const auto status_pos = chunk.find("__HTTP_STATUS__:");
                if (status_pos != std::string::npos) {
                    const std::string before = chunk.substr(0, status_pos);
                    if (!before.empty()) (*on_chunk)(before);
                    try {
                        response.status_code = std::stoi(chunk.substr(status_pos + 16));
                    } catch (...) {}
                } else {
                    if (!(*on_chunk)(chunk)) break;
                }
            }
            body += chunk;
        }

        pclose(pipe);
        unlink(tmp.c_str());

        // Extract status code from body
        const auto status_pos = body.rfind("__HTTP_STATUS__:");
        if (status_pos != std::string::npos) {
            try {
                response.status_code = std::stoi(body.substr(status_pos + 16));
            } catch (...) {}
            body.resize(status_pos);
            // Remove trailing newline before status
            while (!body.empty() && body.back() == '\n') body.pop_back();
        }

        if (!on_chunk) {
            response.body = body;
        }
        return response;
    }

    // No body case (GET requests)
    HttpResponse response;
    std::string body;
    std::array<char, 8192> buf;
    while (true) {
        const size_t n = fread(buf.data(), 1, buf.size(), pipe);
        if (n == 0) break;
        body.append(buf.data(), n);
    }
    pclose(pipe);

    const auto status_pos = body.rfind("__HTTP_STATUS__:");
    if (status_pos != std::string::npos) {
        try {
            response.status_code = std::stoi(body.substr(status_pos + 16));
        } catch (...) {}
        body.resize(status_pos);
        while (!body.empty() && body.back() == '\n') body.pop_back();
    }

    response.body = body;
    return response;
}
