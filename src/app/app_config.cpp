#include "app_config.h"
#include "setup_wizard.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "setup_wizard.h"

namespace {

constexpr const char* kConfigFileName = "bolt.conf";

std::string trim_copy(const std::string& value) {
    const auto begin = value.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) {
        return "";
    }
    const auto end = value.find_last_not_of(" \t\r\n");
    return value.substr(begin, end - begin + 1);
}

std::vector<std::string> split_csv(const std::string& value) {
    std::vector<std::string> items;
    std::stringstream input(value);
    std::string item;
    while (std::getline(input, item, ',')) {
        item = trim_copy(item);
        if (!item.empty()) {
            items.push_back(item);
        }
    }
    return items;
}

void assign_csv_set(std::unordered_set<std::string>* target, const std::string& value) {
    target->clear();
    for (const std::string& item : split_csv(value)) {
        target->insert(item);
    }
}

bool parse_bool(const std::string& key, const std::string& value) {
    std::string normalized = trim_copy(value);
    std::transform(normalized.begin(), normalized.end(), normalized.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });

    if (normalized == "true" || normalized == "1" || normalized == "yes") {
        return true;
    }
    if (normalized == "false" || normalized == "0" || normalized == "no") {
        return false;
    }
    throw std::runtime_error("Invalid boolean for config key: " + key);
}

ApprovalMode parse_approval_mode(const std::string& key, const std::string& value) {
    std::string normalized = trim_copy(value);
    std::transform(normalized.begin(), normalized.end(), normalized.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });

    if (normalized == "prompt") {
        return ApprovalMode::prompt;
    }
    if (normalized == "auto-approve" || normalized == "auto_approve") {
        return ApprovalMode::auto_approve;
    }
    if (normalized == "auto-deny" || normalized == "auto_deny") {
        return ApprovalMode::auto_deny;
    }
    throw std::runtime_error("Invalid approval mode for config key: " + key);
}

int parse_int(const std::string& key, const std::string& value) {
    try {
        return std::stoi(trim_copy(value));
    } catch (const std::exception&) {
        throw std::runtime_error("Invalid integer for config key: " + key);
    }
}

std::size_t parse_size(const std::string& key, const std::string& value) {
    const std::string normalized = trim_copy(value);
    if (!normalized.empty() && normalized[0] == '-') {
        throw std::runtime_error("Invalid size for config key: " + key);
    }

    try {
        return static_cast<std::size_t>(std::stoull(normalized));
    } catch (const std::exception&) {
        throw std::runtime_error("Invalid size for config key: " + key);
    }
}

unsigned short parse_port(const std::string& key, const std::string& value) {
    const int parsed = parse_int(key, value);
    if (parsed <= 0 || parsed > 65535) {
        throw std::runtime_error("Port out of range for config key: " + key);
    }
    return static_cast<unsigned short>(parsed);
}

void apply_config_entry(AppConfig* config, const std::string& key, const std::string& value) {
    if (key == "default_model") {
        config->default_model = value;
        return;
    }
    if (key == "provider") {
        config->provider = value;
        return;
    }
    if (key == "openai.base_url") {
        config->openai_base_url = value;
        return;
    }
    if (key == "openai.model") {
        config->openai_model = value;
        return;
    }
    if (key == "claude.model") {
        config->claude_model = value;
        return;
    }
    if (key == "gemini.model") {
        config->gemini_model = value;
        return;
    }
    if (key == "groq.base_url") {
        config->groq_base_url = value;
        return;
    }
    if (key == "groq.model") {
        config->groq_model = value;
        return;
    }
    if (key == "router.fast_provider") {
        config->router_fast_provider = value;
        return;
    }
    if (key == "router.strong_provider") {
        config->router_strong_provider = value;
        return;
    }
    if (key == "ollama.host") {
        config->ollama.host = value;
        return;
    }
    if (key == "ollama.port") {
        config->ollama.port = parse_port(key, value);
        return;
    }
    if (key == "ollama.path") {
        config->ollama.generate_path = value;
        return;
    }
    if (key == "ollama.resolve_timeout_ms") {
        config->ollama.resolve_timeout_ms = parse_int(key, value);
        return;
    }
    if (key == "ollama.connect_timeout_ms") {
        config->ollama.connect_timeout_ms = parse_int(key, value);
        return;
    }
    if (key == "ollama.send_timeout_ms") {
        config->ollama.send_timeout_ms = parse_int(key, value);
        return;
    }
    if (key == "ollama.receive_timeout_ms") {
        config->ollama.receive_timeout_ms = parse_int(key, value);
        return;
    }
    if (key == "commands.allowed_executables") {
        assign_csv_set(&config->command_policy.allowed_executables, value);
        return;
    }
    if (key == "commands.timeout_ms") {
        config->command_policy.timeout_ms = parse_size(key, value);
        return;
    }
    if (key == "commands.max_output_bytes") {
        config->command_policy.max_output_bytes = parse_size(key, value);
        return;
    }
    if (key == "policy.read_only_tools") {
        assign_csv_set(&config->policy.read_only_tools, value);
        return;
    }
    if (key == "policy.bounded_write_tools") {
        assign_csv_set(&config->policy.bounded_write_tools, value);
        return;
    }
    if (key == "policy.bounded_command_tools") {
        assign_csv_set(&config->policy.bounded_command_tools, value);
        return;
    }
    if (key == "policy.bounded_desktop_tools") {
        assign_csv_set(&config->policy.bounded_desktop_tools, value);
        return;
    }
    if (key == "policy.block_high_risk") {
        config->policy.block_high_risk = parse_bool(key, value);
        return;
    }
    if (key == "agent.default_debug") {
        config->agent_runtime.default_debug = parse_bool(key, value);
        return;
    }
    if (key == "agent.max_model_steps") {
        config->agent_runtime.max_model_steps = parse_int(key, value);
        return;
    }
    if (key == "agent.history_window") {
        config->agent_runtime.history_window = parse_size(key, value);
        return;
    }
    if (key == "agent.history_byte_budget") {
        config->agent_runtime.history_byte_budget = parse_size(key, value);
        return;
    }
    if (key == "agent.max_consecutive_failures") {
        config->agent_runtime.max_consecutive_failures = parse_int(key, value);
        return;
    }
    if (key == "agent.auto_verify") {
        config->agent_runtime.auto_verify = parse_bool(key, value);
        return;
    }
    if (key == "agent.max_auto_verify_retries") {
        config->agent_runtime.max_auto_verify_retries = parse_int(key, value);
        return;
    }
    if (key == "approval.mode") {
        config->approval.mode = parse_approval_mode(key, value);
        return;
    }
    if (key == "sandbox.enabled") {
        config->sandbox.enabled = parse_bool(key, value);
        return;
    }
    if (key == "sandbox.auto_allow_bash") {
        config->sandbox.auto_allow_bash = parse_bool(key, value);
        return;
    }
    if (key == "sandbox.network_enabled") {
        config->sandbox.network_enabled = parse_bool(key, value);
        return;
    }
    if (key == "sandbox.allow_write") {
        config->sandbox.allow_write = split_csv(value);
        return;
    }
    if (key == "sandbox.deny_read") {
        config->sandbox.deny_read = split_csv(value);
        return;
    }

    constexpr const char* subcommand_prefix = "commands.allowed_subcommands.";
    if (key.rfind(subcommand_prefix, 0) == 0) {
        const std::string executable = key.substr(std::string(subcommand_prefix).size());
        if (executable.empty()) {
            throw std::runtime_error("Missing executable name in config key: " + key);
        }

        std::unordered_set<std::string>& allowed =
            config->command_policy.allowed_subcommands[executable];
        assign_csv_set(&allowed, value);
        return;
    }

    throw std::runtime_error("Unknown config key: " + key);
}

void load_config_file(const std::filesystem::path& workspace_root, AppConfig* config) {
    const std::filesystem::path config_path = workspace_root / kConfigFileName;
    if (!std::filesystem::exists(config_path)) {
        return;
    }

    std::ifstream input(config_path);
    if (!input) {
        throw std::runtime_error("Failed to open config file: " + config_path.string());
    }

    std::string line;
    std::size_t line_number = 0;
    while (std::getline(input, line)) {
        ++line_number;
        const std::string trimmed = trim_copy(line);
        if (trimmed.empty() || trimmed[0] == '#') {
            continue;
        }

        const std::size_t equals = trimmed.find('=');
        if (equals == std::string::npos) {
            throw std::runtime_error("Invalid config line " + std::to_string(line_number) +
                                     " in " + config_path.string());
        }

        const std::string key = trim_copy(trimmed.substr(0, equals));
        const std::string value = trim_copy(trimmed.substr(equals + 1));
        if (key.empty()) {
            throw std::runtime_error("Empty config key on line " + std::to_string(line_number) +
                                     " in " + config_path.string());
        }
        apply_config_entry(config, key, value);
    }
}

void apply_env_override(AppConfig* config, const char* env_name, const std::string& key) {
    const char* raw_value = std::getenv(env_name);
    if (raw_value == nullptr) {
        return;
    }
    apply_config_entry(config, key, trim_copy(raw_value));
}

void load_environment_overrides(AppConfig* config) {
    apply_env_override(config, "BOLT_PROVIDER", "provider");
    apply_env_override(config, "BOLT_MODEL", "default_model");
    apply_env_override(config, "BOLT_OPENAI_BASE_URL", "openai.base_url");
    apply_env_override(config, "BOLT_OPENAI_MODEL", "openai.model");
    apply_env_override(config, "BOLT_CLAUDE_MODEL", "claude.model");
    apply_env_override(config, "BOLT_GEMINI_MODEL", "gemini.model");
    apply_env_override(config, "BOLT_GROQ_BASE_URL", "groq.base_url");
    apply_env_override(config, "BOLT_GROQ_MODEL", "groq.model");
    apply_env_override(config, "BOLT_ROUTER_FAST_PROVIDER", "router.fast_provider");
    apply_env_override(config, "BOLT_ROUTER_STRONG_PROVIDER", "router.strong_provider");
    apply_env_override(config, "BOLT_OLLAMA_HOST", "ollama.host");
    apply_env_override(config, "BOLT_OLLAMA_PORT", "ollama.port");
    apply_env_override(config, "BOLT_OLLAMA_PATH", "ollama.path");
    apply_env_override(config, "BOLT_OLLAMA_RESOLVE_TIMEOUT_MS", "ollama.resolve_timeout_ms");
    apply_env_override(config, "BOLT_OLLAMA_CONNECT_TIMEOUT_MS", "ollama.connect_timeout_ms");
    apply_env_override(config, "BOLT_OLLAMA_SEND_TIMEOUT_MS", "ollama.send_timeout_ms");
    apply_env_override(config, "BOLT_OLLAMA_RECEIVE_TIMEOUT_MS", "ollama.receive_timeout_ms");
    apply_env_override(config, "BOLT_ALLOWED_EXECUTABLES", "commands.allowed_executables");
    apply_env_override(config, "BOLT_COMMAND_TIMEOUT_MS", "commands.timeout_ms");
    apply_env_override(config, "BOLT_COMMAND_MAX_OUTPUT_BYTES", "commands.max_output_bytes");
    apply_env_override(config, "BOLT_ALLOWED_GIT_SUBCOMMANDS",
                       "commands.allowed_subcommands.git");
    apply_env_override(config, "BOLT_ALLOWED_OLLAMA_SUBCOMMANDS",
                       "commands.allowed_subcommands.ollama");
    apply_env_override(config, "BOLT_POLICY_READ_ONLY_TOOLS", "policy.read_only_tools");
    apply_env_override(config, "BOLT_POLICY_BOUNDED_WRITE_TOOLS",
                       "policy.bounded_write_tools");
    apply_env_override(config, "BOLT_POLICY_BOUNDED_COMMAND_TOOLS",
                       "policy.bounded_command_tools");
    apply_env_override(config, "BOLT_POLICY_BOUNDED_DESKTOP_TOOLS",
                       "policy.bounded_desktop_tools");
    apply_env_override(config, "BOLT_POLICY_BLOCK_HIGH_RISK", "policy.block_high_risk");
    apply_env_override(config, "BOLT_AGENT_DEFAULT_DEBUG", "agent.default_debug");
    apply_env_override(config, "BOLT_AGENT_MAX_MODEL_STEPS", "agent.max_model_steps");
    apply_env_override(config, "BOLT_AGENT_HISTORY_WINDOW", "agent.history_window");
    apply_env_override(config, "BOLT_AGENT_HISTORY_BYTE_BUDGET",
                       "agent.history_byte_budget");
    apply_env_override(config, "BOLT_AGENT_AUTO_VERIFY", "agent.auto_verify");
    apply_env_override(config, "BOLT_AGENT_MAX_AUTO_VERIFY_RETRIES",
                       "agent.max_auto_verify_retries");
    apply_env_override(config, "BOLT_APPROVAL_MODE", "approval.mode");
    apply_env_override(config, "BOLT_SANDBOX_ENABLED", "sandbox.enabled");
    apply_env_override(config, "BOLT_SANDBOX_NETWORK", "sandbox.network_enabled");
}

void validate_config(const AppConfig& config) {
    if (trim_copy(config.default_model).empty() && config.provider == "ollama") {
        throw std::runtime_error("default_model must not be empty");
    }

    if (config.provider == "ollama" || config.provider == "ollama-chat") {
        if (trim_copy(config.ollama.host).empty()) {
            throw std::runtime_error("ollama.host must not be empty");
        }
        if (trim_copy(config.ollama.generate_path).empty() || config.ollama.generate_path[0] != '/') {
            throw std::runtime_error("ollama.path must start with '/'");
        }

        const std::vector<std::pair<std::string, int>> timeouts = {
            {"ollama.resolve_timeout_ms", config.ollama.resolve_timeout_ms},
            {"ollama.connect_timeout_ms", config.ollama.connect_timeout_ms},
            {"ollama.send_timeout_ms", config.ollama.send_timeout_ms},
            {"ollama.receive_timeout_ms", config.ollama.receive_timeout_ms},
        };
        for (const auto& item : timeouts) {
            if (item.second < 0) {
                throw std::runtime_error(item.first + " must be >= 0");
            }
        }
    }
    if (config.command_policy.allowed_executables.empty()) {
        throw std::runtime_error("commands.allowed_executables must not be empty");
    }
    if (config.command_policy.timeout_ms == 0) {
        throw std::runtime_error("commands.timeout_ms must be > 0");
    }
    if (config.command_policy.max_output_bytes == 0) {
        throw std::runtime_error("commands.max_output_bytes must be > 0");
    }
    if (config.policy.read_only_tools.empty()) {
        throw std::runtime_error("policy.read_only_tools must not be empty");
    }
    if (config.agent_runtime.max_model_steps <= 0) {
        throw std::runtime_error("agent.max_model_steps must be > 0");
    }
    if (config.agent_runtime.history_window == 0) {
        throw std::runtime_error("agent.history_window must be > 0");
    }
    if (config.agent_runtime.history_byte_budget == 0) {
        throw std::runtime_error("agent.history_byte_budget must be > 0");
    }
}

}  // namespace

AppConfig load_app_config(const std::filesystem::path& workspace_root) {
    AppConfig config;
    // Load global user config first (lowest priority)
    load_setup_config(config);
    // Then workspace config overrides
    load_config_file(workspace_root, &config);
    // Then env overrides (highest priority)
    load_environment_overrides(&config);
    validate_config(config);
    return config;
}
