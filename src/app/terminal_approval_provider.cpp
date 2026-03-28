#include "terminal_approval_provider.h"

#include <algorithm>
#include <cctype>
#include <iostream>
#include <string>

namespace {

std::string trim_copy(std::string value) {
    const auto begin = value.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) {
        return "";
    }

    const auto end = value.find_last_not_of(" \t\r\n");
    return value.substr(begin, end - begin + 1);
}

std::string to_lower_copy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return value;
}

std::string preview_args(const std::string& args) {
    constexpr std::size_t kMaxPreviewLength = 240;
    if (args.size() <= kMaxPreviewLength) {
        return args;
    }
    return args.substr(0, kMaxPreviewLength) + "... [truncated]";
}

}  // namespace

TerminalApprovalProvider::TerminalApprovalProvider(std::istream& input, std::ostream& output)
    : input_(input), output_(output) {}

bool TerminalApprovalProvider::approve(const ApprovalRequest& request) {
    output_ << "\n[approval]\n";
    output_ << "tool: " << request.tool_name << "\n";
    output_ << "risk: " << (request.risk.empty() ? "unspecified" : request.risk) << "\n";
    if (!request.reason.empty()) {
        output_ << "reason: " << request.reason << "\n";
    }
    if (!request.preview_summary.empty()) {
        output_ << "summary: " << request.preview_summary << "\n";
    }
    if (!request.preview_details.empty()) {
        output_ << "preview:\n" << request.preview_details << "\n";
    } else if (!request.args.empty()) {
        output_ << "args:\n" << preview_args(request.args) << "\n";
    }
    output_ << "Approve? [y/N]: ";
    output_.flush();

    std::string response;
    if (!std::getline(input_, response)) {
        output_ << "\n";
        return false;
    }

    const std::string normalized = to_lower_copy(trim_copy(response));
    return normalized == "y" || normalized == "yes";
}
