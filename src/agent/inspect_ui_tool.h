#ifndef AGENT_INSPECT_UI_TOOL_H
#define AGENT_INSPECT_UI_TOOL_H

#include <memory>

#include "../core/interfaces/ui_automation.h"
#include "tool.h"

class InspectUiTool : public Tool {
public:
    explicit InspectUiTool(std::shared_ptr<IUiAutomation> ui_automation);

    std::string name() const override;
    std::string description() const override;
    ToolPreview preview(const std::string& args) const override;
    ToolResult run(const std::string& args) const override;

private:
    std::shared_ptr<IUiAutomation> ui_automation_;
};

#endif
