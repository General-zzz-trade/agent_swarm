#include "list_dir_tool.h"

#include <algorithm>
#include <sstream>
#include <vector>

#include "workspace_utils.h"

ListDirTool::ListDirTool(std::filesystem::path workspace_root,
                         std::shared_ptr<IFileSystem> file_system)
    : workspace_root_(std::filesystem::weakly_canonical(std::move(workspace_root))),
      file_system_(std::move(file_system)) {}

std::string ListDirTool::name() const {
    return "list_dir";
}

std::string ListDirTool::description() const {
    return "List files and directories under a workspace-relative path. Args: a relative path or .";
}

ToolResult ListDirTool::run(const std::string& args) const {
    try {
        const std::string raw_path = trim_copy(args);
        const std::filesystem::path target = resolve_workspace_path(workspace_root_, raw_path);

        if (!is_within_workspace(target, workspace_root_)) {
            return {false, "Path is outside the workspace"};
        }
        if (!file_system_->exists(target)) {
            return {false, "Directory does not exist"};
        }
        if (!file_system_->is_directory(target)) {
            return {false, "Path is not a directory"};
        }

        DirectoryListResult directory_list = file_system_->list_directory(target);
        if (!directory_list.success) {
            return {false, directory_list.error};
        }

        std::sort(directory_list.entries.begin(), directory_list.entries.end(),
                  [](const DirectoryEntryInfo& left, const DirectoryEntryInfo& right) {
                      if (left.is_directory != right.is_directory) {
                          return left.is_directory > right.is_directory;
                      }
                      return left.name < right.name;
                  });

        std::ostringstream output;
        output << "DIRECTORY: " << workspace_relative_path(workspace_root_, target) << "\n";

        constexpr std::size_t kMaxEntries = 200;
        const std::size_t count = std::min(directory_list.entries.size(), kMaxEntries);
        for (std::size_t i = 0; i < count; ++i) {
            const auto& entry = directory_list.entries[i];
            if (entry.is_directory) {
                output << "[dir]  " << entry.name << "/\n";
            } else {
                output << "[file] " << entry.name << " (" << entry.size << " bytes)\n";
            }
        }
        if (directory_list.entries.size() > kMaxEntries) {
            output << "[truncated]\n";
        }

        return {true, output.str()};
    } catch (const std::exception& e) {
        return {false, e.what()};
    }
}
