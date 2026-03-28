#ifndef AGENT_WORKSPACE_UTILS_H
#define AGENT_WORKSPACE_UTILS_H

#include <filesystem>
#include <string>

std::string trim_copy(const std::string& value);
bool is_within_workspace(const std::filesystem::path& candidate,
                         const std::filesystem::path& workspace_root);
std::filesystem::path resolve_workspace_path(const std::filesystem::path& workspace_root,
                                             const std::string& raw_path);
std::string workspace_relative_path(const std::filesystem::path& workspace_root,
                                    const std::filesystem::path& target);
bool should_skip_directory_name(const std::string& name);
bool is_text_like_file(const std::filesystem::path& path);

#endif
