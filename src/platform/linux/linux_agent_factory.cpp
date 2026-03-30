#include "linux_agent_factory.h"

#include <memory>

#include "../../agent/agent.h"
#include "../../app/agent_factory.h"
#include "../../app/agent_cli_options.h"
#include "../../app/app_config.h"
#include "../../app/approval_provider_factory.h"
#include "../../app/file_audit_logger.h"
#include "../../app/model_client_factory.h"
#include "linux_command_runner.h"
#include "sandboxed_command_runner.h"
#include "linux_file_system.h"
#include "linux_http_transport.h"
#include "linux_process_manager.h"

AgentServices create_linux_agent_services(const AppConfig& config,
                                          const AgentCliOptions& options,
                                          std::istream& input,
                                          std::ostream& output) {
    AgentServices services;
    services.file_system = std::make_shared<LinuxFileSystem>();
    auto base_runner = std::make_shared<LinuxCommandRunner>();
    if (config.sandbox.enabled) {
        services.command_runner = std::make_shared<SandboxedCommandRunner>(
            base_runner, std::filesystem::current_path(), config.sandbox);
    } else {
        services.command_runner = base_runner;
    }
    services.process_manager = std::make_shared<LinuxProcessManager>();
    // No UI automation or window controller on Linux (desktop tools will be skipped)
    services.ui_automation = nullptr;
    services.window_controller = nullptr;
    services.approval_provider = create_approval_provider(config.approval, input, output);

    auto transport = std::make_shared<LinuxHttpTransport>();
    services.http_transport = transport;
    services.model_client = create_model_client(config, options.model, transport);
    if (services.model_client == nullptr) {
        // Fallback: create Ollama chat client directly
        services.model_client = create_model_client(
            [&]() {
                AppConfig ollama_config = config;
                ollama_config.provider = "ollama-chat";
                return ollama_config;
            }(),
            options.model, transport);
    }
    return services;
}

std::unique_ptr<Agent> create_linux_agent(const std::filesystem::path& workspace_root,
                                          const AppConfig& config,
                                          const AgentCliOptions& options,
                                          std::istream& input,
                                          std::ostream& output) {
    AgentServices services = create_linux_agent_services(config, options, input, output);
    if (services.audit_logger == nullptr) {
        services.audit_logger = std::make_shared<FileAuditLogger>(
            workspace_root / ".bolt" / "audit.log");
    }
    return create_agent(workspace_root, config, options, std::move(services));
}
