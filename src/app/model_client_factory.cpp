#include "model_client_factory.h"

#include <cstdlib>
#include <stdexcept>

#include "../providers/openai_client.h"
#include "../providers/claude_client.h"
#include "../providers/gemini_client.h"
#include "../providers/ollama_chat_client.h"
#include "../core/routing/model_router.h"

namespace {

std::string get_env(const char* name) {
    const char* value = std::getenv(name);
    return value != nullptr ? value : "";
}

[[noreturn]] void throw_missing_key(const std::string& provider, const std::string& env_var,
                                      const std::string& url_hint = "") {
    std::string msg = env_var + " environment variable is required for provider=" + provider + ".\n"
        "  Set it with: export " + env_var + "=your-api-key\n"
        "  Or run: bolt  (setup wizard will guide you)";
    if (!url_hint.empty()) {
        msg += "\n  Get a key at: " + url_hint;
    }
    throw std::runtime_error(msg);
}

}  // namespace

std::unique_ptr<IModelClient> create_model_client(
    const AppConfig& config,
    const std::string& model_override,
    std::shared_ptr<IHttpTransport> transport) {

    const std::string& provider = config.provider;

    if (provider == "openai") {
        OpenAiConfig oai_config;
        oai_config.base_url = config.openai_base_url;
        oai_config.api_key = get_env("OPENAI_API_KEY");
        oai_config.model = model_override.empty() ? config.openai_model : model_override;

        if (oai_config.api_key.empty()) {
            throw_missing_key("openai", "OPENAI_API_KEY", "https://platform.openai.com/api-keys");
        }

        return std::make_unique<OpenAiClient>(std::move(oai_config), transport);
    }

    if (provider == "claude") {
        ClaudeConfig claude_config;
        claude_config.api_key = get_env("ANTHROPIC_API_KEY");
        claude_config.model = model_override.empty() ? config.claude_model : model_override;

        if (claude_config.api_key.empty()) {
            throw_missing_key("claude", "ANTHROPIC_API_KEY", "https://console.anthropic.com/");
        }

        return std::make_unique<ClaudeClient>(std::move(claude_config), transport);
    }

    if (provider == "gemini") {
        GeminiConfig gemini_config;
        gemini_config.api_key = get_env("GEMINI_API_KEY");
        gemini_config.model = model_override.empty() ? config.gemini_model : model_override;

        if (gemini_config.api_key.empty()) {
            throw_missing_key("gemini", "GEMINI_API_KEY", "https://aistudio.google.com/apikey");
        }

        return std::make_unique<GeminiClient>(std::move(gemini_config), transport);
    }

    if (provider == "groq") {
        OpenAiConfig groq_config;
        groq_config.base_url = config.groq_base_url;
        groq_config.api_key = get_env("GROQ_API_KEY");
        groq_config.model = model_override.empty() ? config.groq_model : model_override;
        groq_config.timeout_ms = 30000;  // Groq is fast, shorter timeout

        if (groq_config.api_key.empty()) {
            throw_missing_key("groq", "GROQ_API_KEY", "https://console.groq.com/keys");
        }

        return std::make_unique<OpenAiClient>(std::move(groq_config), transport);
    }

    if (provider == "deepseek") {
        OpenAiConfig cfg;
        cfg.base_url = config.deepseek_base_url;
        cfg.api_key = get_env("DEEPSEEK_API_KEY");
        cfg.model = model_override.empty() ? config.deepseek_model : model_override;

        if (cfg.api_key.empty()) {
            throw_missing_key("deepseek", "DEEPSEEK_API_KEY", "https://platform.deepseek.com/api_keys");
        }

        return std::make_unique<OpenAiClient>(std::move(cfg), transport);
    }

    if (provider == "qwen") {
        OpenAiConfig cfg;
        cfg.base_url = config.qwen_base_url;
        cfg.api_key = get_env("DASHSCOPE_API_KEY");
        cfg.model = model_override.empty() ? config.qwen_model : model_override;

        if (cfg.api_key.empty()) {
            throw_missing_key("qwen", "DASHSCOPE_API_KEY", "https://dashscope.console.aliyun.com/apiKey");
        }

        return std::make_unique<OpenAiClient>(std::move(cfg), transport);
    }

    if (provider == "zhipu") {
        OpenAiConfig cfg;
        cfg.base_url = config.zhipu_base_url;
        cfg.api_key = get_env("ZHIPU_API_KEY");
        cfg.model = model_override.empty() ? config.zhipu_model : model_override;

        if (cfg.api_key.empty()) {
            throw_missing_key("zhipu", "ZHIPU_API_KEY", "https://open.bigmodel.cn/usercenter/apikeys");
        }

        return std::make_unique<OpenAiClient>(std::move(cfg), transport);
    }

    if (provider == "moonshot") {
        OpenAiConfig cfg;
        cfg.base_url = config.moonshot_base_url;
        cfg.api_key = get_env("MOONSHOT_API_KEY");
        cfg.model = model_override.empty() ? config.moonshot_model : model_override;

        if (cfg.api_key.empty()) {
            throw_missing_key("moonshot", "MOONSHOT_API_KEY", "https://platform.moonshot.cn/console/api-keys");
        }

        return std::make_unique<OpenAiClient>(std::move(cfg), transport);
    }

    if (provider == "baichuan") {
        OpenAiConfig cfg;
        cfg.base_url = config.baichuan_base_url;
        cfg.api_key = get_env("BAICHUAN_API_KEY");
        cfg.model = model_override.empty() ? config.baichuan_model : model_override;

        if (cfg.api_key.empty()) {
            throw_missing_key("baichuan", "BAICHUAN_API_KEY", "https://platform.baichuan-ai.com/console/apikey");
        }

        return std::make_unique<OpenAiClient>(std::move(cfg), transport);
    }

    if (provider == "doubao") {
        OpenAiConfig cfg;
        cfg.base_url = config.doubao_base_url;
        cfg.api_key = get_env("VOLC_API_KEY");
        cfg.model = model_override.empty() ? config.doubao_model : model_override;

        if (cfg.api_key.empty()) {
            throw_missing_key("doubao", "VOLC_API_KEY", "https://console.volcengine.com/ark");
        }

        return std::make_unique<OpenAiClient>(std::move(cfg), transport);
    }

    if (provider == "ollama-chat") {
        const std::string model = model_override.empty() ? config.default_model : model_override;
        return std::make_unique<OllamaChatClient>(model, config.ollama, transport);
    }

    if (provider == "router") {
        // Create fast + strong models, then wrap in router
        AppConfig fast_config = config;
        fast_config.provider = config.router_fast_provider;
        auto fast = create_model_client(fast_config, "", transport);
        if (!fast) {
            throw std::runtime_error("Router fast provider '" + config.router_fast_provider +
                                     "' returned null");
        }

        AppConfig strong_config = config;
        strong_config.provider = config.router_strong_provider;
        auto strong = create_model_client(strong_config, "", transport);
        if (!strong) {
            throw std::runtime_error("Router strong provider '" + config.router_strong_provider +
                                     "' returned null");
        }

        return std::make_unique<ModelRouter>(std::move(fast), std::move(strong));
    }

    // Default: legacy ollama (provider == "ollama")
    // Return nullptr to signal the caller should use the legacy WindowsOllamaModelClient
    return nullptr;
}
