#include "click_element_tool.h"

#include <sstream>
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

ClickElementTarget parse_target(const std::string& raw_args) {
    ClickElementTarget target;
    std::istringstream input(raw_args);
    std::string line;
    while (std::getline(input, line)) {
        const std::string trimmed = trim_copy(line);
        if (trimmed.empty()) {
            continue;
        }
        if (trimmed.rfind("handle=", 0) == 0 || trimmed.rfind("handle:", 0) == 0) {
            target.element_handle = trim_copy(trimmed.substr(7));
            continue;
        }
        if (trimmed.rfind("window_handle=", 0) == 0 || trimmed.rfind("window_handle:", 0) == 0) {
            target.window_handle = trim_copy(trimmed.substr(14));
            continue;
        }
        if (trimmed.rfind("text=", 0) == 0 || trimmed.rfind("text:", 0) == 0) {
            target.text = trim_copy(trimmed.substr(5));
            continue;
        }
        if (trimmed.rfind("class=", 0) == 0 || trimmed.rfind("class:", 0) == 0) {
            target.class_name = trim_copy(trimmed.substr(6));
            continue;
        }
        if (target.text.empty()) {
            target.text = trimmed;
            continue;
        }
        throw std::runtime_error("click_element received an unexpected argument: " + trimmed);
    }

    if (target.element_handle.empty() && target.text.empty() && target.class_name.empty()) {
        throw std::runtime_error("click_element requires an element handle or a text/class selector");
    }
    return target;
}

std::string summarize_target(const ClickElementTarget& target) {
    if (!target.element_handle.empty()) {
        return "handle=" + target.element_handle;
    }

    std::ostringstream output;
    if (!target.text.empty()) {
        output << "text=" << target.text;
    }
    if (!target.class_name.empty()) {
        if (output.tellp() > 0) {
            output << ", ";
        }
        output << "class=" << target.class_name;
    }
    return output.str();
}

}  // namespace

ClickElementTool::ClickElementTool(std::shared_ptr<IUiAutomation> ui_automation,
                                   std::shared_ptr<IAuditLogger> audit_logger)
    : ui_automation_(std::move(ui_automation)),
      audit_logger_(std::move(audit_logger)) {
    if (ui_automation_ == nullptr) {
        throw std::invalid_argument("ClickElementTool requires UI automation support");
    }
}

std::string ClickElementTool::name() const {
    return "click_element";
}

std::string ClickElementTool::description() const {
    return "Click or focus a UI element in the foreground window by handle or text/class selector. Approval is required.";
}

ToolPreview ClickElementTool::preview(const std::string& args) const {
    try {
        const ClickElementTarget target = parse_target(args);
        return {"Click UI element", summarize_target(target)};
    } catch (const std::exception& error) {
        return {"Unable to preview click_element request", error.what()};
    }
}

ToolResult ClickElementTool::run(const std::string& args) const {
    try {
        const ClickElementTarget target = parse_target(args);
        const std::string summary = summarize_target(target);
        const ClickElementResult result = ui_automation_->click_element(target);
        if (!result.success) {
            safe_log_audit(audit_logger_,
                           {"desktop", "tool_error", name(), summary, "", 0, -1, false, false,
                            false, result.error});
            return {false, result.error};
        }

        safe_log_audit(audit_logger_,
                       {"desktop", "executed", name(), result.element.handle, "", 0, -1, true,
                        true, false,
                        "class=" + result.element.class_name + " text=" + result.element.text});

        std::ostringstream output;
        output << "CLICKED ELEMENT\n";
        output << "handle: " << result.element.handle << "\n";
        output << "class: " << result.element.class_name << "\n";
        output << "text: " << result.element.text;
        return {true, output.str()};
    } catch (const std::exception& error) {
        safe_log_audit(audit_logger_,
                       {"desktop", "tool_error", name(), trim_copy(args), "", 0, -1, false,
                        false, false, error.what()});
        return {false, error.what()};
    }
}
