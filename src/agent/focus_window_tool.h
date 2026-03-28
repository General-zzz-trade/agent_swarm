#ifndef AGENT_FOCUS_WINDOW_TOOL_H
#define AGENT_FOCUS_WINDOW_TOOL_H

#include <memory>

#include "../core/interfaces/audit_logger.h"
#include "../core/interfaces/window_controller.h"
#include "tool.h"

class FocusWindowTool : public Tool {
public:
    FocusWindowTool(std::shared_ptr<IWindowController> window_controller,
                    std::shared_ptr<IAuditLogger> audit_logger = nullptr);

    std::string name() const override;
    std::string description() const override;
    ToolPreview preview(const std::string& args) const override;
    ToolResult run(const std::string& args) const override;

private:
    std::shared_ptr<IWindowController> window_controller_;
    std::shared_ptr<IAuditLogger> audit_logger_;
};

#endif
