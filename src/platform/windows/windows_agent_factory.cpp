#include "windows_agent_factory.h"

#include <memory>

#include "../../agent/agent.h"
#include "../../app/agent_factory.h"
#include "../../app/agent_cli_options.h"
#include "../../app/app_config.h"
#include "../../app/approval_provider_factory.h"
#include "../../app/file_audit_logger.h"
#include "../../app/model_client_factory.h"
#include "winhttp_transport.h"
#include "windows_command_runner.h"
#include "windows_file_system.h"
#include "windows_ollama_model_client.h"
#include "windows_process_manager.h"
#include "windows_ui_automation.h"
#include "windows_window_controller.h"

AgentServices create_windows_agent_services(const AppConfig& config,
                                            const AgentCliOptions& options,
                                            std::istream& input,
                                            std::ostream& output) {
    AgentServices services;
    services.file_system = std::make_shared<WindowsFileSystem>();
    services.command_runner = std::make_shared<WindowsCommandRunner>();
    services.process_manager = std::make_shared<WindowsProcessManager>();
    services.ui_automation = std::make_shared<WindowsUiAutomation>();
    services.window_controller = std::make_shared<WindowsWindowController>();
    services.approval_provider = create_approval_provider(config.approval, input, output);

    auto transport = std::make_shared<WinHttpTransport>();
    services.model_client = create_model_client(config, options.model, transport);
    if (services.model_client == nullptr) {
        // Fallback: legacy Ollama generate client
        services.model_client =
            std::make_unique<WindowsOllamaModelClient>(options.model, config.ollama, transport);
    }
    return services;
}

std::unique_ptr<Agent> create_windows_agent(const std::filesystem::path& workspace_root,
                                            const AppConfig& config,
                                            const AgentCliOptions& options,
                                            std::istream& input,
                                            std::ostream& output) {
    AgentServices services = create_windows_agent_services(config, options, input, output);
    if (services.audit_logger == nullptr) {
        services.audit_logger = std::make_shared<FileAuditLogger>(
            workspace_root / ".mini_nn" / "audit.log");
    }
    return create_agent(workspace_root, config, options, std::move(services));
}
