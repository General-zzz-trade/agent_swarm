#include "list_processes_tool.h"

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

bool matches_filter(const ProcessInfo& info, const std::string& filter) {
    if (filter.empty()) {
        return true;
    }
    return to_lower_copy(info.executable_name).find(to_lower_copy(filter)) != std::string::npos;
}

}  // namespace

ListProcessesTool::ListProcessesTool(std::shared_ptr<IProcessManager> process_manager)
    : process_manager_(std::move(process_manager)) {
    if (process_manager_ == nullptr) {
        throw std::invalid_argument("ListProcessesTool requires a process manager");
    }
}

std::string ListProcessesTool::name() const {
    return "list_processes";
}

std::string ListProcessesTool::description() const {
    return "List running processes. Args may optionally contain a case-insensitive process-name filter.";
}

ToolPreview ListProcessesTool::preview(const std::string& args) const {
    const std::string filter = trim_copy(args);
    if (filter.empty()) {
        return {"List running processes", "No filter provided."};
    }
    return {"List running processes", "Filter: " + filter};
}

ToolResult ListProcessesTool::run(const std::string& args) const {
    const std::string filter = trim_copy(args);
    const ProcessListResult result = process_manager_->list_processes();
    if (!result.success) {
        return {false, result.error};
    }

    std::vector<ProcessInfo> matches;
    for (const ProcessInfo& info : result.processes) {
        if (matches_filter(info, filter)) {
            matches.push_back(info);
        }
    }

    std::sort(matches.begin(), matches.end(),
              [](const ProcessInfo& left, const ProcessInfo& right) {
                  if (left.executable_name != right.executable_name) {
                      return left.executable_name < right.executable_name;
                  }
                  return left.process_id < right.process_id;
              });

    constexpr std::size_t kMaxRows = 60;
    std::ostringstream output;
    output << "MATCHES: " << matches.size() << "\n";
    if (!filter.empty()) {
        output << "FILTER: " << filter << "\n";
    }

    const std::size_t rows = std::min(matches.size(), kMaxRows);
    for (std::size_t i = 0; i < rows; ++i) {
        output << matches[i].process_id << " " << matches[i].executable_name << "\n";
    }
    if (matches.size() > rows) {
        output << "[truncated " << (matches.size() - rows) << " processes]\n";
    }

    return {true, output.str()};
}
