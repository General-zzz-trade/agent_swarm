#include "search_code_tool.h"

#include <sstream>
#include <string>

#include "workspace_utils.h"

namespace {

std::string shorten_line(const std::string& line) {
    constexpr std::size_t kMaxLength = 180;
    if (line.size() <= kMaxLength) {
        return line;
    }
    return line.substr(0, kMaxLength) + "...";
}

}  // namespace

SearchCodeTool::SearchCodeTool(std::filesystem::path workspace_root,
                               std::shared_ptr<IFileSystem> file_system)
    : workspace_root_(std::filesystem::weakly_canonical(std::move(workspace_root))),
      file_system_(std::move(file_system)) {}

std::string SearchCodeTool::name() const {
    return "search_code";
}

std::string SearchCodeTool::description() const {
    return "Search plain text recursively across workspace source files. Args: the exact text to search for.";
}

ToolResult SearchCodeTool::run(const std::string& args) const {
    try {
        const std::string query = trim_copy(args);
        if (query.empty()) {
            return {false, "Expected search text"};
        }

        std::ostringstream output;
        output << "SEARCH: " << query << "\n";

        constexpr std::size_t kMaxMatches = 30;
        constexpr std::uintmax_t kMaxFileBytes = 1024 * 1024;
        const TextSearchResult search_result =
            file_system_->search_text(workspace_root_, query, kMaxMatches, kMaxFileBytes);
        if (!search_result.success) {
            return {false, search_result.error};
        }

        for (const auto& match : search_result.matches) {
            output << match.relative_path
                   << ":" << match.line_number << ": "
                   << shorten_line(match.line) << "\n";
        }

        if (search_result.matches.empty()) {
            output << "No matches found.\n";
        }
        if (search_result.truncated) {
            output << "[truncated]\n";
        }

        return {true, output.str()};
    } catch (const std::exception& e) {
        return {false, e.what()};
    }
}
