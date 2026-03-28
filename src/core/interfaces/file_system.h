#ifndef CORE_INTERFACES_FILE_SYSTEM_H
#define CORE_INTERFACES_FILE_SYSTEM_H

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

struct FileReadResult {
    bool success;
    std::string content;
    std::string error;
};

struct FileWriteResult {
    bool success;
    std::string error;
};

struct DirectoryEntryInfo {
    bool is_directory;
    std::string name;
    std::uintmax_t size;
};

struct DirectoryListResult {
    bool success;
    std::vector<DirectoryEntryInfo> entries;
    std::string error;
};

struct TextSearchMatch {
    std::string relative_path;
    std::size_t line_number;
    std::string line;
};

struct TextSearchResult {
    bool success;
    std::vector<TextSearchMatch> matches;
    bool truncated;
    std::string error;
};

class IFileSystem {
public:
    virtual ~IFileSystem() = default;

    virtual bool exists(const std::filesystem::path& path) const = 0;
    virtual bool is_directory(const std::filesystem::path& path) const = 0;
    virtual bool is_regular_file(const std::filesystem::path& path) const = 0;
    virtual DirectoryListResult list_directory(const std::filesystem::path& path) const = 0;
    virtual bool create_directories(const std::filesystem::path& path,
                                    std::string& error) const = 0;
    virtual FileReadResult read_text_file(const std::filesystem::path& path) const = 0;
    virtual FileWriteResult write_text_file(const std::filesystem::path& path,
                                            const std::string& content) const = 0;
    virtual TextSearchResult search_text(const std::filesystem::path& root,
                                         const std::string& query,
                                         std::size_t max_matches,
                                         std::uintmax_t max_file_bytes) const = 0;
};

#endif
