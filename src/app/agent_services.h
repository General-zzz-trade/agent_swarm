#ifndef APP_AGENT_SERVICES_H
#define APP_AGENT_SERVICES_H

#include <memory>

#include "../core/interfaces/audit_logger.h"
#include "../core/interfaces/approval_provider.h"
#include "../core/interfaces/command_runner.h"
#include "../core/interfaces/file_system.h"
#include "../core/interfaces/model_client.h"
#include "../core/interfaces/process_manager.h"
#include "../core/interfaces/ui_automation.h"
#include "../core/interfaces/window_controller.h"

struct AgentServices {
    std::unique_ptr<IModelClient> model_client;
    std::shared_ptr<IFileSystem> file_system;
    std::shared_ptr<ICommandRunner> command_runner;
    std::shared_ptr<IProcessManager> process_manager;
    std::shared_ptr<IUiAutomation> ui_automation;
    std::shared_ptr<IWindowController> window_controller;
    std::shared_ptr<IApprovalProvider> approval_provider;
    std::shared_ptr<IAuditLogger> audit_logger;
};

#endif
