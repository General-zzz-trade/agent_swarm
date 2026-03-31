#include "agent_factory.h"

#include <algorithm>
#include <stdexcept>
#include <thread>
#include <utility>

#include "../agent/agent.h"
#include "../agent/task_planner_tool.h"
#include "../agent/tool_set_factory.h"
#include "../core/threading/thread_pool.h"
#include "agent_cli_options.h"
#include "app_config.h"
#include "model_client_factory.h"
#include "null_audit_logger.h"
#include "static_approval_provider.h"

namespace {

std::unique_ptr<IModelClient> create_worker_model_client(const AppConfig& config,
                                                         const AgentCliOptions& options,
                                                         const std::shared_ptr<IHttpTransport>& transport) {
    std::unique_ptr<IModelClient> client = create_model_client(config, options.model, transport);
    if (client) {
        return client;
    }

    AppConfig fallback = config;
    fallback.provider = "ollama-chat";
    return create_model_client(fallback, options.model, transport);
}

void wire_task_planner_workers(ToolRegistry& tools,
                               const std::filesystem::path& workspace_root,
                               const AppConfig& config,
                               const AgentCliOptions& options,
                               const std::shared_ptr<IFileSystem>& file_system,
                               const std::shared_ptr<ICommandRunner>& command_runner,
                               const std::shared_ptr<IProcessManager>& process_manager,
                               const std::shared_ptr<IUiAutomation>& ui_automation,
                               const std::shared_ptr<IWindowController>& window_controller,
                               const std::shared_ptr<IAuditLogger>& audit_logger,
                               const std::shared_ptr<IHttpTransport>& http_transport) {
    auto* task_planner = dynamic_cast<TaskPlannerTool*>(tools.find_mutable("task_planner"));
    if (task_planner == nullptr) {
        return;
    }

    const unsigned int concurrency = std::max(2u, std::thread::hardware_concurrency());
    auto swarm_pool = std::make_shared<ThreadPool>(concurrency);

    task_planner->set_swarm_support(
        swarm_pool,
        [workspace_root, config, options, file_system, command_runner, process_manager,
         ui_automation, window_controller, audit_logger, http_transport]() -> std::unique_ptr<Agent> {
            AppConfig worker_config = config;
            worker_config.agent_runtime.auto_verify = false;
            worker_config.agent_runtime.max_model_steps =
                std::min(worker_config.agent_runtime.max_model_steps, 12);

            std::unique_ptr<IModelClient> worker_client =
                create_worker_model_client(worker_config, options, http_transport);
            if (!worker_client) {
                throw std::runtime_error("Failed to create worker model client");
            }

            ToolRegistry worker_tools = create_default_tool_registry(
                workspace_root, file_system, command_runner, audit_logger,
                worker_config.command_policy, process_manager, ui_automation,
                window_controller, http_transport);

            // Worker agents are exploratory: any approval-required action is auto-denied.
            auto worker_approval = std::make_shared<StaticApprovalProvider>(false);

            return std::make_unique<Agent>(std::move(worker_client),
                                           std::move(worker_approval),
                                           workspace_root,
                                           worker_config.policy,
                                           worker_config.agent_runtime,
                                           false,
                                           audit_logger,
                                           std::move(worker_tools));
        });
}

}  // namespace

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
                                                      services.window_controller,
                                                      services.http_transport);
    wire_task_planner_workers(tools, workspace_root, config, options,
                              services.file_system, services.command_runner,
                              services.process_manager, services.ui_automation,
                              services.window_controller, audit_logger,
                              services.http_transport);

    return std::make_unique<Agent>(std::move(services.model_client),
                                   std::move(services.approval_provider),
                                   workspace_root,
                                   config.policy,
                                   config.agent_runtime,
                                   options.debug,
                                   std::move(audit_logger),
                                   std::move(tools));
}
