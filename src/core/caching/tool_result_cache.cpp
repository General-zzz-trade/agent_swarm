#include "tool_result_cache.h"

ToolResultCache::ToolResultCache(int ttl_seconds)
    : ttl_seconds_(ttl_seconds) {}

const ToolResultCache::Entry* ToolResultCache::get(
    const std::string& tool_name, const std::string& args) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const std::string key = make_key(tool_name, args);
    auto it = cache_.find(key);
    if (it == cache_.end() || is_expired(it->second)) {
        ++misses_;
        return nullptr;
    }
    ++hits_;
    return &it->second;
}

void ToolResultCache::put(const std::string& tool_name, const std::string& args,
                           bool success, const std::string& content) {
    std::lock_guard<std::mutex> lock(mutex_);
    const std::string key = make_key(tool_name, args);
    cache_[key] = {success, content, std::chrono::steady_clock::now()};
}

void ToolResultCache::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    cache_.clear();
}

std::size_t ToolResultCache::size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return cache_.size();
}

std::string ToolResultCache::make_key(const std::string& tool_name,
                                       const std::string& args) {
    return tool_name + "\0" + args;
}

bool ToolResultCache::is_expired(const Entry& entry) const {
    const auto elapsed = std::chrono::steady_clock::now() - entry.created;
    return std::chrono::duration_cast<std::chrono::seconds>(elapsed).count() > ttl_seconds_;
}
