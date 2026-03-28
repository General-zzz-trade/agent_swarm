#include "list_windows_tool.h"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <stdexcept>
#include <vector>

#include "workspace_utils.h"

namespace {

std::string to_lower_copy(const std::string& value) {
    std::string lowered = value;
    std::transform(lowered.begin(), lowered.end(), lowered.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return lowered;
}

bool matches_filter(const WindowInfo& info, const std::string& filter) {
    if (filter.empty()) {
        return true;
    }
    const std::string lowered_filter = to_lower_copy(filter);
    return to_lower_copy(info.title).find(lowered_filter) != std::string::npos ||
           to_lower_copy(info.class_name).find(lowered_filter) != std::string::npos;
}

}  // namespace

ListWindowsTool::ListWindowsTool(std::shared_ptr<IWindowController> window_controller)
    : window_controller_(std::move(window_controller)) {
    if (window_controller_ == nullptr) {
        throw std::invalid_argument("ListWindowsTool requires a window controller");
    }
}

std::string ListWindowsTool::name() const {
    return "list_windows";
}

std::string ListWindowsTool::description() const {
    return "List visible top-level windows. Args may optionally contain a case-insensitive title or class filter.";
}

ToolPreview ListWindowsTool::preview(const std::string& args) const {
    const std::string filter = trim_copy(args);
    if (filter.empty()) {
        return {"List visible windows", "No filter provided."};
    }
    return {"List visible windows", "Filter: " + filter};
}

ToolResult ListWindowsTool::run(const std::string& args) const {
    const std::string filter = trim_copy(args);
    const WindowListResult result = window_controller_->list_windows();
    if (!result.success) {
        return {false, result.error};
    }

    std::vector<WindowInfo> matches;
    for (const WindowInfo& info : result.windows) {
        if (matches_filter(info, filter)) {
            matches.push_back(info);
        }
    }

    std::sort(matches.begin(), matches.end(),
              [](const WindowInfo& left, const WindowInfo& right) {
                  if (left.title != right.title) {
                      return left.title < right.title;
                  }
                  return left.handle < right.handle;
              });

    constexpr std::size_t kMaxRows = 40;
    std::ostringstream output;
    output << "MATCHES: " << matches.size() << "\n";
    if (!filter.empty()) {
        output << "FILTER: " << filter << "\n";
    }

    const std::size_t rows = std::min(matches.size(), kMaxRows);
    for (std::size_t i = 0; i < rows; ++i) {
        output << matches[i].handle << " pid=" << matches[i].process_id
               << " class=" << matches[i].class_name
               << " title=" << matches[i].title << "\n";
    }
    if (matches.size() > rows) {
        output << "[truncated " << (matches.size() - rows) << " windows]\n";
    }

    return {true, output.str()};
}
