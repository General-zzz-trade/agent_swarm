#include "windows_file_system.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <system_error>
#include <unordered_set>

namespace {

bool should_skip_directory_name(const std::string& name) {
    static const std::unordered_set<std::string> skipped_names = {
        ".git",
        ".idea",
        "build",
        "cmake-build-debug",
    };
    return skipped_names.find(name) != skipped_names.end();
}

bool is_text_like_file(const std::filesystem::path& path) {
    static const std::unordered_set<std::string> text_extensions = {
        ".c",   ".cc",   ".cpp",  ".cxx", ".h",   ".hh",  ".hpp",  ".hxx",
        ".txt", ".md",   ".cmake",".json",".toml",".yaml",".yml",  ".py",
        ".js",  ".ts",   ".tsx",  ".java",".cs",  ".rs",  ".go",   ".html",
        ".css", ".xml",  ".ini",  ".sh",  ".ps1",
    };

    std::string extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return text_extensions.find(extension) != text_extensions.end();
}

}  // namespace

bool WindowsFileSystem::exists(const std::filesystem::path& path) const {
    return std::filesystem::exists(path);
}

bool WindowsFileSystem::is_directory(const std::filesystem::path& path) const {
    return std::filesystem::is_directory(path);
}

bool WindowsFileSystem::is_regular_file(const std::filesystem::path& path) const {
    return std::filesystem::is_regular_file(path);
}

DirectoryListResult WindowsFileSystem::list_directory(const std::filesystem::path& path) const {
    try {
        std::vector<DirectoryEntryInfo> entries;
        for (const auto& entry : std::filesystem::directory_iterator(path)) {
            entries.push_back({entry.is_directory(),
                               entry.path().filename().string(),
                               entry.is_regular_file() ? entry.file_size() : 0});
        }
        return {true, std::move(entries), ""};
    } catch (const std::exception& e) {
        return {false, {}, e.what()};
    }
}

bool WindowsFileSystem::create_directories(const std::filesystem::path& path,
                                           std::string& error) const {
    try {
        if (path.empty()) {
            return true;
        }
        std::filesystem::create_directories(path);
        return true;
    } catch (const std::exception& e) {
        error = e.what();
        return false;
    }
}

FileReadResult WindowsFileSystem::read_text_file(const std::filesystem::path& path) const {
    try {
        std::ifstream input(path, std::ios::binary);
        if (!input) {
            return {false, "", "Unable to open file"};
        }

        std::ostringstream buffer;
        buffer << input.rdbuf();
        return {true, buffer.str(), ""};
    } catch (const std::exception& e) {
        return {false, "", e.what()};
    }
}

FileWriteResult WindowsFileSystem::write_text_file(const std::filesystem::path& path,
                                                   const std::string& content) const {
    try {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        if (!output) {
            return {false, "Unable to open file for writing"};
        }

        output << content;
        output.close();
        return {true, ""};
    } catch (const std::exception& e) {
        return {false, e.what()};
    }
}

TextSearchResult WindowsFileSystem::search_text(const std::filesystem::path& root,
                                                const std::string& query,
                                                std::size_t max_matches,
                                                std::uintmax_t max_file_bytes) const {
    try {
        std::vector<TextSearchMatch> matches;
        bool truncated = false;

        std::filesystem::recursive_directory_iterator iterator(
            root, std::filesystem::directory_options::skip_permission_denied);
        for (const auto& entry : iterator) {
            if (entry.is_directory() &&
                should_skip_directory_name(entry.path().filename().string())) {
                iterator.disable_recursion_pending();
                continue;
            }

            if (!entry.is_regular_file() || !is_text_like_file(entry.path())) {
                continue;
            }

            std::error_code file_size_error;
            const auto file_size = std::filesystem::file_size(entry.path(), file_size_error);
            if (!file_size_error && file_size > max_file_bytes) {
                continue;
            }

            std::ifstream input(entry.path());
            if (!input) {
                continue;
            }

            std::string line;
            std::size_t line_number = 0;
            while (std::getline(input, line)) {
                ++line_number;
                if (line.find(query) == std::string::npos) {
                    continue;
                }

                matches.push_back({std::filesystem::relative(entry.path(), root)
                                       .lexically_normal()
                                       .string(),
                                   line_number,
                                   line});
                if (matches.size() >= max_matches) {
                    truncated = true;
                    return {true, std::move(matches), truncated, ""};
                }
            }
        }

        return {true, std::move(matches), truncated, ""};
    } catch (const std::exception& e) {
        return {false, {}, false, e.what()};
    }
}
