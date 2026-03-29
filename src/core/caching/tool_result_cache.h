#ifndef CORE_CACHING_TOOL_RESULT_CACHE_H
#define CORE_CACHING_TOOL_RESULT_CACHE_H

#include <chrono>
#include <mutex>
#include <string>
#include <unordered_map>

/// Caches results of read-only tool calls to avoid redundant execution.
///
/// Zero quality loss: only caches tools that are explicitly read-only
/// (read_file, list_dir, search_code, calculator, list_processes, etc.)
/// Write tools are NEVER cached.
///
/// Cache is keyed on (tool_name + args). Entries expire after a configurable TTL.
/// This saves a full model round-trip when the model asks to read the same file
/// it already read earlier in the conversation.
class ToolResultCache {
public:
    struct Entry {
        bool success;
        std::string content;
        std::chrono::steady_clock::time_point created;
    };

    explicit ToolResultCache(int ttl_seconds = 60);

    /// Try to get a cached result. Returns nullptr if not cached or expired.
    const Entry* get(const std::string& tool_name, const std::string& args) const;

    /// Store a tool result in the cache.
    void put(const std::string& tool_name, const std::string& args,
             bool success, const std::string& content);

    /// Clear all cached entries.
    void clear();

    /// Number of cached entries.
    std::size_t size() const;

    /// Number of cache hits since creation.
    std::size_t hits() const { return hits_; }

    /// Number of cache misses since creation.
    std::size_t misses() const { return misses_; }

private:
    int ttl_seconds_;
    mutable std::mutex mutex_;
    std::unordered_map<std::string, Entry> cache_;
    mutable std::size_t hits_ = 0;
    mutable std::size_t misses_ = 0;

    static std::string make_key(const std::string& tool_name, const std::string& args);
    bool is_expired(const Entry& entry) const;
};

#endif
