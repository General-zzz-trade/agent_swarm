#include "agent_factory.h"

#include <stdexcept>
#include <utility>

#include "../agent/agent.h"
#include "../agent/tool_set_factory.h"
#include "agent_cli_options.h"
#include "app_config.h"
#include "null_audit_logger.h"

std::unique_ptr<Agent> create_agent(const std::filesystem::path& workspace_root,
                                    const AppConfig& config,
                                    const AgentCliOptions& options,
                                    AgentServices services) {
    if (services.model_client == nullptr) {
        throw std::invalid_argument("Agent services require a model client");
    }
    if (services.file_system == nullptr) {
        throw std::invalid_argument("Agent services require a file system");
    }
    if (services.command_runner == nullptr) {
        throw std::invalid_argument("Agent services require a command runner");
    }
    if (services.approval_provider == nullptr) {
        throw std::invalid_argument("Agent services require an approval provider");
    }

    std::shared_ptr<IAuditLogger> audit_logger =
        services.audit_logger == nullptr ? std::make_shared<NullAuditLogger>()
                                         : std::move(services.audit_logger);
    ToolRegistry tools = create_default_tool_registry(workspace_root, services.file_system,
                                                      services.command_runner, audit_logger,
                                                      config.command_policy,
                                                      services.process_manager,
                                                      services.ui_automation,
                                                      services.window_controller);

    return std::make_unique<Agent>(std::move(services.model_client),
                                   std::move(services.approval_provider),
                                   workspace_root,
                                   config.policy,
                                   config.agent_runtime,
                                   options.debug,
                                   std::move(audit_logger),
                                   std::move(tools));
}
