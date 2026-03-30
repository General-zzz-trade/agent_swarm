#ifndef APP_APP_CONFIG_H
#define APP_APP_CONFIG_H

#include <filesystem>
#include <string>

#include "../core/config/agent_runtime_config.h"
#include "../core/config/approval_config.h"
#include "../core/config/command_policy_config.h"
#include "../core/config/ollama_connection_config.h"
#include "../core/config/policy_config.h"
#include "../core/config/sandbox_config.h"

struct AppConfig {
    std::string default_model = "qwen3:8b";
    std::string provider = "ollama";  // ollama | ollama-chat | openai | claude | gemini | groq | router
    std::string openai_base_url = "https://api.openai.com";
    std::string openai_model = "gpt-4o";
    std::string claude_model = "claude-sonnet-4-20250514";
    std::string gemini_model = "gemini-2.0-flash";
    std::string groq_base_url = "https://api.groq.com/openai";
    std::string groq_model = "llama-3.3-70b-versatile";
    // Router: fast model (groq) + strong model (claude/openai)
    std::string router_fast_provider = "groq";
    std::string router_strong_provider = "claude";
    OllamaConnectionConfig ollama;
    CommandPolicyConfig command_policy;
    PolicyConfig policy;
    AgentRuntimeConfig agent_runtime;
    ApprovalConfig approval;
    SandboxConfig sandbox;
};

AppConfig load_app_config(const std::filesystem::path& workspace_root);

#endif
