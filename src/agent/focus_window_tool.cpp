#include "focus_window_tool.h"

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

WindowFocusTarget parse_focus_target(const std::string& raw_args) {
    const std::string args = trim_copy(raw_args);
    if (args.empty()) {
        throw std::runtime_error("focus_window requires a window title or handle");
    }

    WindowFocusTarget target;
    if (args.rfind("handle=", 0) == 0 || args.rfind("handle:", 0) == 0) {
        target.handle = trim_copy(args.substr(7));
    } else if (args.rfind("title=", 0) == 0 || args.rfind("title:", 0) == 0) {
        target.title = trim_copy(args.substr(6));
    } else {
        target.title = args;
    }

    if (target.handle.empty() && target.title.empty()) {
        throw std::runtime_error("focus_window target is empty");
    }
    return target;
}

std::string target_summary(const WindowFocusTarget& target) {
    if (!target.handle.empty()) {
        return "handle=" + target.handle;
    }
    return "title=" + target.title;
}

}  // namespace

FocusWindowTool::FocusWindowTool(std::shared_ptr<IWindowController> window_controller,
                                 std::shared_ptr<IAuditLogger> audit_logger)
    : window_controller_(std::move(window_controller)),
      audit_logger_(std::move(audit_logger)) {
    if (window_controller_ == nullptr) {
        throw std::invalid_argument("FocusWindowTool requires a window controller");
    }
}

std::string FocusWindowTool::name() const {
    return "focus_window";
}

std::string FocusWindowTool::description() const {
    return "Focus a visible top-level window by exact title or handle. Approval is required.";
}

ToolPreview FocusWindowTool::preview(const std::string& args) const {
    try {
        const WindowFocusTarget target = parse_focus_target(args);
        return {"Focus window", target_summary(target)};
    } catch (const std::exception& error) {
        return {"Unable to preview focus_window request", error.what()};
    }
}

ToolResult FocusWindowTool::run(const std::string& args) const {
    try {
        const WindowFocusTarget target = parse_focus_target(args);
        const std::string summary = target_summary(target);
        const WindowFocusResult result = window_controller_->focus_window(target);
        if (!result.success) {
            safe_log_audit(audit_logger_,
                           {"desktop", "tool_error", name(), summary, "", 0, -1, false,
                            false, false, result.error});
            return {false, result.error};
        }

        safe_log_audit(audit_logger_,
                       {"desktop", "executed", name(), result.window.handle, "", 0, -1, true,
                        true, false, "title=" + result.window.title});
        return {true, "FOCUSED WINDOW\nhandle: " + result.window.handle +
                           "\npid: " + std::to_string(result.window.process_id) +
                           "\nclass: " + result.window.class_name +
                           "\ntitle: " + result.window.title};
    } catch (const std::exception& error) {
        safe_log_audit(audit_logger_,
                       {"desktop", "tool_error", name(), trim_copy(args), "", 0, -1, false,
                        false, false, error.what()});
        return {false, error.what()};
    }
}
