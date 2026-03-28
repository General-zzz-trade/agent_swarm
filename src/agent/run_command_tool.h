#ifndef AGENT_RUN_COMMAND_TOOL_H
#define AGENT_RUN_COMMAND_TOOL_H

#include <filesystem>
#include <memory>

#include "../core/interfaces/audit_logger.h"
#include "../core/interfaces/command_runner.h"
#include "../core/config/command_policy_config.h"
#include "tool.h"

class RunCommandTool : public Tool {
public:
    RunCommandTool(std::filesystem::path workspace_root,
                   std::shared_ptr<ICommandRunner> command_runner,
                   std::shared_ptr<IAuditLogger> audit_logger = nullptr,
                   CommandPolicyConfig config = {});

    std::string name() const override;
    std::string description() const override;
    ToolPreview preview(const std::string& args) const override;
    ToolResult run(const std::string& args) const override;

private:
    std::filesystem::path workspace_root_;
    std::shared_ptr<ICommandRunner> command_runner_;
    std::shared_ptr<IAuditLogger> audit_logger_;
    CommandPolicyConfig config_;
};

#endif
