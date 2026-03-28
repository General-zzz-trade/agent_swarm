#include "open_app_tool.h"

#include <stdexcept>

#include "workspace_utils.h"

namespace {

void safe_log_audit(const std::shared_ptr<IAuditLogger>& audit_logger, const AuditEvent& event) {
    if (audit_logger == nullptr) {
        return;
    }

    try {
        audit_logger->log(event);
    } catch (...) {
    }
}

bool contains_blocked_metacharacters(const std::string& command_line) {
    for (const char ch : command_line) {
        switch (ch) {
            case '&':
            case '|':
            case ';':
            case '<':
            case '>':
            case '^':
            case '\n':
            case '\r':
                return true;
            default:
                break;
        }
    }
    return false;
}

}  // namespace

OpenAppTool::OpenAppTool(std::shared_ptr<IProcessManager> process_manager,
                         std::shared_ptr<IAuditLogger> audit_logger)
    : process_manager_(std::move(process_manager)),
      audit_logger_(std::move(audit_logger)) {
    if (process_manager_ == nullptr) {
        throw std::invalid_argument("OpenAppTool requires a process manager");
    }
}

std::string OpenAppTool::name() const {
    return "open_app";
}

std::string OpenAppTool::description() const {
    return "Launch a local application by exact command line. Approval is required.";
}

ToolPreview OpenAppTool::preview(const std::string& args) const {
    const std::string command_line = trim_copy(args);
    if (command_line.empty()) {
        return {"Open application", "No command line provided."};
    }
    return {"Open application", "Command line: " + command_line};
}

ToolResult OpenAppTool::run(const std::string& args) const {
    try {
        const std::string command_line = trim_copy(args);
        if (command_line.empty()) {
            safe_log_audit(audit_logger_,
                           {"desktop", "validation_failed", name(), "", "", 0, -1, false,
                            false, false, "Command line is empty"});
            return {false, "Command line is empty"};
        }
        if (contains_blocked_metacharacters(command_line)) {
            safe_log_audit(audit_logger_,
                           {"desktop", "validation_failed", name(), command_line, "", 0, -1,
                            false, false, false,
                            "Command line contains blocked shell metacharacters"});
            return {false, "Command line contains blocked shell metacharacters"};
        }

        const LaunchProcessResult result = process_manager_->launch_process(command_line);
        if (!result.success) {
            safe_log_audit(audit_logger_,
                           {"desktop", "tool_error", name(), command_line, "", 0, -1, false,
                            false, false, result.error});
            return {false, result.error};
        }

        safe_log_audit(audit_logger_,
                       {"desktop", "executed", name(), command_line, "", 0,
                        static_cast<int>(result.process_id), true, true, false,
                        "pid=" + std::to_string(result.process_id)});
        return {true, "OPENED APPLICATION\nPID: " + std::to_string(result.process_id) +
                           "\nCommand line: " + command_line};
    } catch (const std::exception& error) {
        safe_log_audit(audit_logger_,
                       {"desktop", "tool_error", name(), trim_copy(args), "", 0, -1, false,
                        false, false, error.what()});
        return {false, error.what()};
    }
}
