#include "file_prefetch.h"

#include <fstream>
#include <regex>

FilePrefetchCache::FilePrefetchCache(ThreadPool& pool, std::size_t max_cache_bytes)
    : pool_(pool), max_cache_bytes_(max_cache_bytes) {}

void FilePrefetchCache::warm(const std::filesystem::path& path) {
    const std::string key = path.string();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = cache_.find(key);
        if (it != cache_.end()) return;  // Already cached or loading
        cache_[key] = {"", 0, true};  // Mark as loading
    }

    pool_.submit([this, key]() { do_prefetch(key); });
}

std::string FilePrefetchCache::get(const std::filesystem::path& path) const {
    const std::string key = path.string();
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = cache_.find(key);
    if (it == cache_.end() || it->second.loading) return "";
    return it->second.content;
}

bool FilePrefetchCache::contains(const std::filesystem::path& path) const {
    const std::string key = path.string();
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = cache_.find(key);
    return it != cache_.end() && !it->second.loading;
}

void FilePrefetchCache::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    cache_.clear();
    current_bytes_.store(0, std::memory_order_relaxed);
}

void FilePrefetchCache::on_streaming_token(const std::string& accumulated_text,
                                            const std::filesystem::path& workspace_root) {
    const auto paths = extract_file_paths(accumulated_text);
    for (const auto& rel_path : paths) {
        auto full_path = workspace_root / rel_path;
        std::error_code ec;
        if (std::filesystem::exists(full_path, ec) && std::filesystem::is_regular_file(full_path, ec)) {
            warm(full_path);
        }
    }
}

std::size_t FilePrefetchCache::cached_count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::size_t count = 0;
    for (const auto& [_, entry] : cache_) {
        if (!entry.loading) ++count;
    }
    return count;
}

std::size_t FilePrefetchCache::cached_bytes() const {
    return current_bytes_.load(std::memory_order_relaxed);
}

std::vector<std::string> FilePrefetchCache::extract_file_paths(const std::string& text) const {
    std::vector<std::string> paths;

    // Match common file path patterns:
    // - src/foo/bar.cpp, include/header.h, CMakeLists.txt
    // - "path": "src/main.cpp" in JSON
    // - read_file, write_file, edit_file tool args with path=
    static const std::regex path_pattern(
        R"((?:path[=:]\s*|"path"\s*:\s*")([a-zA-Z0-9_./-]+\.[a-zA-Z0-9]+))",
        std::regex::optimize);

    std::sregex_iterator it(text.begin(), text.end(), path_pattern);
    std::sregex_iterator end;
    for (; it != end; ++it) {
        const std::string match = (*it)[1].str();
        if (match.size() > 2 && match.size() < 260) {
            paths.push_back(match);
        }
    }

    // Also look for bare file paths like src/agent/agent.cpp
    static const std::regex bare_path_pattern(
        R"(\b((?:src|include|tests|lib|third_party)/[a-zA-Z0-9_/.-]+\.[a-zA-Z0-9]+)\b)",
        std::regex::optimize);

    std::sregex_iterator it2(text.begin(), text.end(), bare_path_pattern);
    for (; it2 != end; ++it2) {
        const std::string match = (*it2)[1].str();
        if (match.size() > 2 && match.size() < 260) {
            paths.push_back(match);
        }
    }

    return paths;
}

void FilePrefetchCache::do_prefetch(const std::string& path) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) {
        std::lock_guard<std::mutex> lock(mutex_);
        cache_.erase(path);
        return;
    }

    const auto file_size = static_cast<std::size_t>(input.tellg());

    // Skip files that are too large (>4MB)
    if (file_size > 4 * 1024 * 1024) {
        std::lock_guard<std::mutex> lock(mutex_);
        cache_.erase(path);
        return;
    }

    // Check if adding this would exceed budget
    if (current_bytes_.load(std::memory_order_relaxed) + file_size > max_cache_bytes_) {
        std::lock_guard<std::mutex> lock(mutex_);
        cache_.erase(path);
        return;
    }

    input.seekg(0, std::ios::beg);
    std::string content(file_size, '\0');
    input.read(content.data(), static_cast<std::streamsize>(file_size));

    std::lock_guard<std::mutex> lock(mutex_);
    auto it = cache_.find(path);
    if (it != cache_.end()) {
        it->second.content = std::move(content);
        it->second.size = file_size;
        it->second.loading = false;
        current_bytes_.fetch_add(file_size, std::memory_order_relaxed);
    }
}
