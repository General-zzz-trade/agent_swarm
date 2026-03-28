#ifndef APP_APP_CONFIG_H
#define APP_APP_CONFIG_H

#include <filesystem>
#include <string>

#include "../core/config/agent_runtime_config.h"
#include "../core/config/approval_config.h"
#include "../core/config/command_policy_config.h"
#include "../core/config/ollama_connection_config.h"
#include "../core/config/policy_config.h"

struct AppConfig {
    std::string default_model = "qwen3:8b";
    OllamaConnectionConfig ollama;
    CommandPolicyConfig command_policy;
    PolicyConfig policy;
    AgentRuntimeConfig agent_runtime;
    ApprovalConfig approval;
};

AppConfig load_app_config(const std::filesystem::path& workspace_root);

#endif
