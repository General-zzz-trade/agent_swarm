#ifndef AGENT_LIST_PROCESSES_TOOL_H
#define AGENT_LIST_PROCESSES_TOOL_H

#include <memory>

#include "../core/interfaces/process_manager.h"
#include "tool.h"

class ListProcessesTool : public Tool {
public:
    explicit ListProcessesTool(std::shared_ptr<IProcessManager> process_manager);

    std::string name() const override;
    std::string description() const override;
    ToolPreview preview(const std::string& args) const override;
    ToolResult run(const std::string& args) const override;

private:
    std::shared_ptr<IProcessManager> process_manager_;
};

#endif
