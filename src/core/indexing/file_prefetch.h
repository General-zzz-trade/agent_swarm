#ifndef CORE_INDEXING_FILE_PREFETCH_H
#define CORE_INDEXING_FILE_PREFETCH_H

#include <atomic>
#include <filesystem>
#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "../threading/thread_pool.h"

/// Speculative file prefetch cache.
/// Warms file content into memory during LLM streaming, before the tool call is executed.
/// When the agent's streaming tokens hint at a file path, prefetch it in the background.
class FilePrefetchCache {
public:
    explicit FilePrefetchCache(ThreadPool& pool, std::size_t max_cache_bytes = 64 * 1024 * 1024);

    /// Prefetch a file into cache (non-blocking, submitted to thread pool).
    void warm(const std::filesystem::path& path);

    /// Get cached file content. Returns empty string if not cached.
    std::string get(const std::filesystem::path& path) const;

    /// Check if a path is already cached.
    bool contains(const std::filesystem::path& path) const;

    /// Clear the cache.
    void clear();

    /// Analyze partial streaming tokens for file path hints and prefetch them.
    /// Call this from the streaming callback to speculatively prefetch.
    void on_streaming_token(const std::string& accumulated_text,
                            const std::filesystem::path& workspace_root);

    /// Cache statistics
    std::size_t cached_count() const;
    std::size_t cached_bytes() const;

private:
    struct CacheEntry {
        std::string content;
        std::size_t size;
        bool loading;  // true if currently being loaded
    };

    ThreadPool& pool_;
    std::size_t max_cache_bytes_;
    mutable std::mutex mutex_;
    std::unordered_map<std::string, CacheEntry> cache_;
    std::atomic<std::size_t> current_bytes_{0};

    // Pattern detection for speculative prefetch
    std::vector<std::string> extract_file_paths(const std::string& text) const;
    void do_prefetch(const std::string& path);
};

#endif
