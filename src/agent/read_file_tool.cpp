#include "read_file_tool.h"

#include <filesystem>
#include <stdexcept>

#include "workspace_utils.h"

ReadFileTool::ReadFileTool(std::filesystem::path workspace_root,
                           std::shared_ptr<IFileSystem> file_system)
    : workspace_root_(std::filesystem::weakly_canonical(std::move(workspace_root))),
      file_system_(std::move(file_system)) {
    if (file_system_ == nullptr) {
        throw std::invalid_argument("ReadFileTool requires a file system");
    }
}

std::string ReadFileTool::name() const {
    return "read_file";
}

std::string ReadFileTool::description() const {
    return "Read a text file inside the current workspace. Args must be a relative path.";
}

ToolResult ReadFileTool::run(const std::string& args) const {
    try {
        const std::string trimmed = trim_copy(args);
        if (trimmed.empty()) {
            return {false, "Expected a relative file path"};
        }

        std::filesystem::path target = resolve_workspace_path(workspace_root_, trimmed);

        if (!is_within_workspace(target, workspace_root_)) {
            return {false, "Path is outside the workspace"};
        }
        if (!file_system_->exists(target)) {
            return {false, "File does not exist"};
        }
        if (!file_system_->is_regular_file(target)) {
            return {false, "Path is not a regular file"};
        }

        const FileReadResult read_result = file_system_->read_text_file(target);
        if (!read_result.success) {
            return {false, read_result.error};
        }

        std::string content = read_result.content;
        constexpr std::size_t kMaxBytes = 32000;  // was 4KB — 32KB for real files
        if (content.size() > kMaxBytes) {
            content.resize(kMaxBytes);
            content += "\n[truncated]";
        }

        return {true, "FILE: " + workspace_relative_path(workspace_root_, target) + "\n" + content};
    } catch (const std::exception& e) {
        return {false, e.what()};
    }
}
