#ifndef AGENT_WAIT_FOR_WINDOW_TOOL_H
#define AGENT_WAIT_FOR_WINDOW_TOOL_H

#include <memory>

#include "../core/interfaces/window_controller.h"
#include "tool.h"

class WaitForWindowTool : public Tool {
public:
    explicit WaitForWindowTool(std::shared_ptr<IWindowController> window_controller);

    std::string name() const override;
    std::string description() const override;
    ToolPreview preview(const std::string& args) const override;
    ToolResult run(const std::string& args) const override;

private:
    std::shared_ptr<IWindowController> window_controller_;
};

#endif
