#ifndef AGENT_OPEN_APP_TOOL_H
#define AGENT_OPEN_APP_TOOL_H

#include <memory>

#include "../core/interfaces/audit_logger.h"
#include "../core/interfaces/process_manager.h"
#include "tool.h"

class OpenAppTool : public Tool {
public:
    OpenAppTool(std::shared_ptr<IProcessManager> process_manager,
                std::shared_ptr<IAuditLogger> audit_logger = nullptr);

    std::string name() const override;
    std::string description() const override;
    ToolPreview preview(const std::string& args) const override;
    ToolResult run(const std::string& args) const override;

private:
    std::shared_ptr<IProcessManager> process_manager_;
    std::shared_ptr<IAuditLogger> audit_logger_;
};

#endif
