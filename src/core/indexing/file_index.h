#ifndef CORE_INDEXING_FILE_INDEX_H
#define CORE_INDEXING_FILE_INDEX_H

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

/// Memory-mapped trigram index for near-instant code search.
/// Builds an in-memory trigram→file_positions map for all text files in a directory.
/// Search complexity: O(matches) instead of O(total_bytes).
class FileIndex {
public:
    struct SearchResult {
        std::string file_path;
        std::size_t line_number;
        std::string line_content;
    };

    /// Build index over all text files under root_dir.
    void build(const std::filesystem::path& root_dir,
               const std::vector<std::string>& extensions = {".cpp", ".h", ".hpp", ".c", ".txt", ".md", ".json", ".cmake"});

    /// Search for a query string. Returns matching lines.
    std::vector<SearchResult> search(const std::string& query, std::size_t max_results = 100) const;

    /// Number of indexed files.
    std::size_t file_count() const;

    /// Total bytes indexed.
    std::size_t total_bytes() const;

    /// Whether index has been built.
    bool is_ready() const { return ready_.load(std::memory_order_acquire); }

private:
    struct FileEntry {
        std::string path;
        std::string content;  // file content kept in memory
        std::vector<std::size_t> line_offsets;  // offset of each line start
    };

    // Trigram: 3-byte sequence packed into uint32_t
    using Trigram = uint32_t;

    struct TrigramHit {
        uint32_t file_index;
        uint32_t byte_offset;
    };

    static Trigram make_trigram(char a, char b, char c) {
        return (static_cast<uint32_t>(static_cast<unsigned char>(a)) << 16) |
               (static_cast<uint32_t>(static_cast<unsigned char>(b)) << 8) |
               static_cast<uint32_t>(static_cast<unsigned char>(c));
    }

    std::vector<FileEntry> files_;
    std::unordered_map<Trigram, std::vector<TrigramHit>> trigram_index_;
    std::atomic<bool> ready_{false};
    std::size_t total_bytes_ = 0;
    mutable std::mutex search_mutex_;

    void index_file(uint32_t file_idx);
    std::size_t find_line(const FileEntry& entry, std::size_t byte_offset) const;
    bool is_text_file(const std::filesystem::path& path, const std::vector<std::string>& extensions) const;
};

#endif
