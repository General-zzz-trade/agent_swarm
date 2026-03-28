#ifndef AGENT_TOOL_SET_FACTORY_H
#define AGENT_TOOL_SET_FACTORY_H

#include <filesystem>
#include <memory>

#include "../core/config/command_policy_config.h"
#include "../core/interfaces/audit_logger.h"
#include "../core/interfaces/command_runner.h"
#include "../core/interfaces/file_system.h"
#include "../core/interfaces/process_manager.h"
#include "../core/interfaces/ui_automation.h"
#include "../core/interfaces/window_controller.h"
#include "tool_registry.h"

ToolRegistry create_default_tool_registry(
    const std::filesystem::path& workspace_root,
    const std::shared_ptr<IFileSystem>& file_system,
    const std::shared_ptr<ICommandRunner>& command_runner,
    const std::shared_ptr<IAuditLogger>& audit_logger,
    CommandPolicyConfig command_policy = {},
    const std::shared_ptr<IProcessManager>& process_manager = nullptr,
    const std::shared_ptr<IUiAutomation>& ui_automation = nullptr,
    const std::shared_ptr<IWindowController>& window_controller = nullptr);

#endif
