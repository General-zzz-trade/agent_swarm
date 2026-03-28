#include "type_text_tool.h"

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

std::string parse_text_argument(const std::string& raw_args) {
    const std::string block_marker = "text<<<";
    const std::size_t block_start = raw_args.find(block_marker);
    if (block_start != std::string::npos) {
        const std::size_t content_start = block_start + block_marker.size();
        const std::size_t end_marker = raw_args.find("\n>>>", content_start);
        if (end_marker == std::string::npos) {
            throw std::runtime_error("type_text is missing the closing >>> marker");
        }

        std::string text = raw_args.substr(content_start, end_marker - content_start);
        if (!text.empty() && text.front() == '\n') {
            text.erase(text.begin());
        }
        return text;
    }

    const std::string trimmed = trim_copy(raw_args);
    if (trimmed.rfind("text=", 0) == 0 || trimmed.rfind("text:", 0) == 0) {
        return trim_copy(trimmed.substr(5));
    }
    return trimmed;
}

std::string summarize_text(const std::string& text) {
    constexpr std::size_t kPreviewLimit = 120;
    std::string summary =
        text.size() <= kPreviewLimit ? text : text.substr(0, kPreviewLimit) + "...";
    if (summary.empty()) {
        summary = "[empty]";
    }
    return summary;
}

}  // namespace

TypeTextTool::TypeTextTool(std::shared_ptr<IUiAutomation> ui_automation,
                           std::shared_ptr<IAuditLogger> audit_logger)
    : ui_automation_(std::move(ui_automation)),
      audit_logger_(std::move(audit_logger)) {
    if (ui_automation_ == nullptr) {
        throw std::invalid_argument("TypeTextTool requires UI automation support");
    }
}

std::string TypeTextTool::name() const {
    return "type_text";
}

std::string TypeTextTool::description() const {
    return "Type text into the currently focused window. Approval is required.";
}

ToolPreview TypeTextTool::preview(const std::string& args) const {
    try {
        const std::string text = parse_text_argument(args);
        std::ostringstream details;
        details << "characters=" << text.size() << "\n";
        details << summarize_text(text);
        return {"Type text into the focused window", details.str()};
    } catch (const std::exception& error) {
        return {"Unable to preview type_text request", error.what()};
    }
}

ToolResult TypeTextTool::run(const std::string& args) const {
    try {
        const std::string text = parse_text_argument(args);
        if (text.empty()) {
            safe_log_audit(audit_logger_,
                           {"desktop", "validation_failed", name(), "chars=0", "", 0, -1, false,
                            false, false, "Text is empty"});
            return {false, "Text is empty"};
        }

        const TypeTextResult result = ui_automation_->type_text(text);
        if (!result.success) {
            safe_log_audit(audit_logger_,
                           {"desktop", "tool_error", name(),
                            "chars=" + std::to_string(text.size()), "", 0, -1, false, false,
                            false, result.error});
            return {false, result.error};
        }

        safe_log_audit(audit_logger_,
                       {"desktop", "executed", name(),
                        "chars=" + std::to_string(text.size()), "", 0, -1, true, true, false,
                        "sent_chars=" + std::to_string(result.characters_sent)});
        return {true, "TYPED TEXT\ncharacters: " + std::to_string(result.characters_sent)};
    } catch (const std::exception& error) {
        safe_log_audit(audit_logger_,
                       {"desktop", "tool_error", name(), "chars=unknown", "", 0, -1, false,
                        false, false, error.what()});
        return {false, error.what()};
    }
}
