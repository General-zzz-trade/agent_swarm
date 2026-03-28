#ifndef AGENT_LIST_WINDOWS_TOOL_H
#define AGENT_LIST_WINDOWS_TOOL_H

#include <memory>

#include "../core/interfaces/window_controller.h"
#include "tool.h"

class ListWindowsTool : public Tool {
public:
    explicit ListWindowsTool(std::shared_ptr<IWindowController> window_controller);

    std::string name() const override;
    std::string description() const override;
    ToolPreview preview(const std::string& args) const override;
    ToolResult run(const std::string& args) const override;

private:
    std::shared_ptr<IWindowController> window_controller_;
};

#endif
