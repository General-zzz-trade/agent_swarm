#ifndef PLATFORM_WINDOWS_WINDOWS_FILE_SYSTEM_H
#define PLATFORM_WINDOWS_WINDOWS_FILE_SYSTEM_H

#include "../../core/interfaces/file_system.h"

class WindowsFileSystem : public IFileSystem {
public:
    bool exists(const std::filesystem::path& path) const override;
    bool is_directory(const std::filesystem::path& path) const override;
    bool is_regular_file(const std::filesystem::path& path) const override;
    DirectoryListResult list_directory(const std::filesystem::path& path) const override;
    bool create_directories(const std::filesystem::path& path,
                            std::string& error) const override;
    FileReadResult read_text_file(const std::filesystem::path& path) const override;
    FileWriteResult write_text_file(const std::filesystem::path& path,
                                    const std::string& content) const override;
    TextSearchResult search_text(const std::filesystem::path& root,
                                 const std::string& query,
                                 std::size_t max_matches,
                                 std::uintmax_t max_file_bytes) const override;
};

#endif
