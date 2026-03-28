#ifndef AGENT_TYPE_TEXT_TOOL_H
#define AGENT_TYPE_TEXT_TOOL_H

#include <memory>

#include "../core/interfaces/audit_logger.h"
#include "../core/interfaces/ui_automation.h"
#include "tool.h"

class TypeTextTool : public Tool {
public:
    TypeTextTool(std::shared_ptr<IUiAutomation> ui_automation,
                 std::shared_ptr<IAuditLogger> audit_logger);

    std::string name() const override;
    std::string description() const override;
    ToolPreview preview(const std::string& args) const override;
    ToolResult run(const std::string& args) const override;

private:
    std::shared_ptr<IUiAutomation> ui_automation_;
    std::shared_ptr<IAuditLogger> audit_logger_;
};

#endif
