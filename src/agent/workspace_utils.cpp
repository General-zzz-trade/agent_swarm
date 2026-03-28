#include "workspace_utils.h"

#include <algorithm>
#include <cctype>
#include <unordered_set>

std::string trim_copy(const std::string& value) {
    const auto begin = value.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) {
        return "";
    }
    const auto end = value.find_last_not_of(" \t\r\n");
    return value.substr(begin, end - begin + 1);
}

bool is_within_workspace(const std::filesystem::path& candidate,
                         const std::filesystem::path& workspace_root) {
    auto candidate_it = candidate.begin();
    auto workspace_it = workspace_root.begin();

    for (; workspace_it != workspace_root.end(); ++workspace_it, ++candidate_it) {
        if (candidate_it == candidate.end() || *candidate_it != *workspace_it) {
            return false;
        }
    }
    return true;
}

std::filesystem::path resolve_workspace_path(const std::filesystem::path& workspace_root,
                                             const std::string& raw_path) {
    const std::string trimmed = trim_copy(raw_path);
    const std::filesystem::path target =
        trimmed.empty() ? workspace_root : workspace_root / trimmed;
    return std::filesystem::weakly_canonical(target);
}

std::string workspace_relative_path(const std::filesystem::path& workspace_root,
                                    const std::filesystem::path& target) {
    const auto relative = std::filesystem::relative(target, workspace_root).lexically_normal();
    const std::string value = relative.string();
    return value.empty() ? "." : value;
}

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
