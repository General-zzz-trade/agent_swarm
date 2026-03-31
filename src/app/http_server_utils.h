#ifndef APP_HTTP_SERVER_UTILS_H
#define APP_HTTP_SERVER_UTILS_H

#include <cstddef>
#include <cctype>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>

namespace http_server_utils {

inline constexpr std::size_t kMaxHttpHeaderBytes = 1024 * 1024;
inline constexpr std::size_t kMaxHttpBodyBytes = 512 * 1024;

inline int hex_value(char ch) {
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'f') return 10 + (ch - 'a');
    if (ch >= 'A' && ch <= 'F') return 10 + (ch - 'A');
    return -1;
}

inline std::string decode_url_path(const std::string& raw) {
    std::string decoded;
    decoded.reserve(raw.size());

    for (std::size_t i = 0; i < raw.size(); ++i) {
        const char ch = raw[i];
        if (ch == '%') {
            if (i + 2 >= raw.size()) {
                throw std::runtime_error("Invalid percent-encoded path");
            }
            const int hi = hex_value(raw[i + 1]);
            const int lo = hex_value(raw[i + 2]);
            if (hi < 0 || lo < 0) {
                throw std::runtime_error("Invalid percent-encoded path");
            }
            const char decoded_char = static_cast<char>((hi << 4) | lo);
            if (decoded_char == '\0' || static_cast<unsigned char>(decoded_char) < 0x20) {
                throw std::runtime_error("Invalid control character in path");
            }
            decoded.push_back(decoded_char);
            i += 2;
            continue;
        }

        decoded.push_back(ch == '\\' ? '/' : ch);
    }

    return decoded;
}

inline std::string normalize_request_path(const std::string& target) {
    const std::size_t path_end = target.find_first_of("?#");
    const std::string raw_path = target.substr(0, path_end);
    std::string normalized = decode_url_path(raw_path.empty() ? "/" : raw_path);
    if (normalized.empty()) normalized = "/";
    if (normalized.front() != '/') normalized.insert(normalized.begin(), '/');
    return normalized;
}

inline bool has_parent_reference(const std::string& normalized_path) {
    for (const auto& part : std::filesystem::path(normalized_path)) {
        if (part == "..") return true;
    }
    return false;
}

inline bool path_is_within(const std::filesystem::path& root,
                           const std::filesystem::path& candidate) {
    auto root_it = root.begin();
    auto candidate_it = candidate.begin();
    for (; root_it != root.end(); ++root_it, ++candidate_it) {
        if (candidate_it == candidate.end() || *candidate_it != *root_it) {
            return false;
        }
    }
    return true;
}

inline std::optional<std::filesystem::path> resolve_static_file(
    const std::filesystem::path& base_dir,
    const std::string& request_path) {
    namespace fs = std::filesystem;

    if (!fs::exists(base_dir) || !fs::is_directory(base_dir)) {
        return std::nullopt;
    }

    const std::string normalized = normalize_request_path(request_path);
    if (has_parent_reference(normalized)) {
        return std::nullopt;
    }

    fs::path relative = fs::path(normalized).relative_path();
    if (relative.empty()) {
        relative = "index.html";
    }

    std::error_code ec;
    const fs::path root = fs::weakly_canonical(base_dir, ec);
    if (ec) return std::nullopt;

    const fs::path candidate = fs::weakly_canonical(root / relative, ec);
    if (ec || !path_is_within(root, candidate)) {
        return std::nullopt;
    }

    if (!fs::exists(candidate, ec) || !fs::is_regular_file(candidate, ec)) {
        return std::nullopt;
    }

    return candidate;
}

inline bool looks_like_spa_route(const std::string& request_path) {
    const std::string normalized = normalize_request_path(request_path);
    if (has_parent_reference(normalized)) {
        return false;
    }
    return !std::filesystem::path(normalized).has_extension();
}

inline bool constant_time_equals(const std::string& expected,
                                 const std::string& actual) {
    if (expected.size() != actual.size()) return false;

    unsigned char diff = 0;
    for (std::size_t i = 0; i < expected.size(); ++i) {
        diff |= static_cast<unsigned char>(expected[i] ^ actual[i]);
    }
    return diff == 0;
}

inline std::optional<std::string> extract_api_token(
    const std::unordered_map<std::string, std::string>& headers) {
    const auto auth_it = headers.find("authorization");
    if (auth_it != headers.end()) {
        const std::string& value = auth_it->second;
        if (value.size() >= 7) {
            std::string prefix = value.substr(0, 7);
            for (char& ch : prefix) {
                ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
            }
            if (prefix == "bearer ") {
                return value.substr(7);
            }
        }
    }

    const auto token_it = headers.find("x-bolt-api-token");
    if (token_it != headers.end()) {
        return token_it->second;
    }

    return std::nullopt;
}

inline bool is_loopback_bind_address(const std::string& address) {
    return address == "127.0.0.1" || address == "localhost";
}

}  // namespace http_server_utils

#endif
