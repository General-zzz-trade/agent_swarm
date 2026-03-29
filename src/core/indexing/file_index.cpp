#include "file_index.h"

#include <algorithm>
#include <fstream>
#include <set>

void FileIndex::build(const std::filesystem::path& root_dir,
                      const std::vector<std::string>& extensions) {
    files_.clear();
    trigram_index_.clear();
    total_bytes_ = 0;
    ready_.store(false, std::memory_order_release);

    // Collect all matching files
    std::error_code ec;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(root_dir, ec)) {
        if (!entry.is_regular_file()) continue;
        if (!is_text_file(entry.path(), extensions)) continue;

        // Skip hidden dirs and build dirs
        const std::string path_str = entry.path().string();
        if (path_str.find(".git") != std::string::npos) continue;
        if (path_str.find("build") != std::string::npos &&
            path_str.find("CMakeFiles") != std::string::npos) continue;

        // Read file
        std::ifstream input(entry.path(), std::ios::binary);
        if (!input) continue;

        std::string content((std::istreambuf_iterator<char>(input)),
                            std::istreambuf_iterator<char>());

        // Skip binary files (files with null bytes in first 8KB)
        const std::size_t check_len = std::min(content.size(), static_cast<std::size_t>(8192));
        bool is_binary = false;
        for (std::size_t i = 0; i < check_len; ++i) {
            if (content[i] == '\0') {
                is_binary = true;
                break;
            }
        }
        if (is_binary) continue;

        // Build line offsets
        std::vector<std::size_t> line_offsets;
        line_offsets.push_back(0);
        for (std::size_t i = 0; i < content.size(); ++i) {
            if (content[i] == '\n' && i + 1 < content.size()) {
                line_offsets.push_back(i + 1);
            }
        }

        total_bytes_ += content.size();
        files_.push_back({entry.path().string(), std::move(content), std::move(line_offsets)});
    }

    // Build trigram index
    for (uint32_t i = 0; i < static_cast<uint32_t>(files_.size()); ++i) {
        index_file(i);
    }

    ready_.store(true, std::memory_order_release);
}

void FileIndex::index_file(uint32_t file_idx) {
    const std::string& content = files_[file_idx].content;
    if (content.size() < 3) return;

    for (uint32_t i = 0; i + 2 < static_cast<uint32_t>(content.size()); ++i) {
        const Trigram tri = make_trigram(content[i], content[i + 1], content[i + 2]);
        trigram_index_[tri].push_back({file_idx, i});
    }
}

std::vector<FileIndex::SearchResult> FileIndex::search(const std::string& query,
                                                        std::size_t max_results) const {
    if (query.size() < 3 || !ready_.load(std::memory_order_acquire)) {
        // For short queries, fall back to brute force
        std::vector<SearchResult> results;
        for (const auto& file : files_) {
            std::size_t pos = 0;
            while (pos < file.content.size() && results.size() < max_results) {
                pos = file.content.find(query, pos);
                if (pos == std::string::npos) break;

                const std::size_t line_num = find_line(file, pos);
                const std::size_t line_start = file.line_offsets[line_num];
                const std::size_t line_end = file.content.find('\n', line_start);
                const std::string line = file.content.substr(
                    line_start,
                    line_end == std::string::npos ? std::string::npos : line_end - line_start);

                results.push_back({file.path, line_num + 1, line});
                pos += query.size();
            }
        }
        return results;
    }

    // Use trigram index: intersect posting lists of all trigrams in query
    const Trigram first_tri = make_trigram(query[0], query[1], query[2]);
    auto it = trigram_index_.find(first_tri);
    if (it == trigram_index_.end()) return {};

    // Start with first trigram's hits
    std::vector<SearchResult> results;
    const auto& candidates = it->second;

    for (const auto& hit : candidates) {
        if (results.size() >= max_results) break;

        const FileEntry& file = files_[hit.file_index];
        const uint32_t offset = hit.byte_offset;

        // Verify full query match at this position
        if (offset + query.size() > file.content.size()) continue;
        if (file.content.compare(offset, query.size(), query) != 0) continue;

        const std::size_t line_num = find_line(file, offset);
        const std::size_t line_start = file.line_offsets[line_num];
        const std::size_t line_end = file.content.find('\n', line_start);
        const std::string line = file.content.substr(
            line_start,
            line_end == std::string::npos ? std::string::npos : line_end - line_start);

        results.push_back({file.path, line_num + 1, line});
    }

    return results;
}

std::size_t FileIndex::find_line(const FileEntry& entry, std::size_t byte_offset) const {
    // Binary search for the line containing byte_offset
    auto it = std::upper_bound(entry.line_offsets.begin(), entry.line_offsets.end(), byte_offset);
    if (it == entry.line_offsets.begin()) return 0;
    return static_cast<std::size_t>(std::distance(entry.line_offsets.begin(), --it));
}

std::size_t FileIndex::file_count() const {
    return files_.size();
}

std::size_t FileIndex::total_bytes() const {
    return total_bytes_;
}

bool FileIndex::is_text_file(const std::filesystem::path& path,
                             const std::vector<std::string>& extensions) const {
    const std::string ext = path.extension().string();
    for (const auto& allowed : extensions) {
        if (ext == allowed) return true;
    }
    return false;
}
