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
            throw std::runtime_error("OPENAI_API_KEY environment variable is required for provider=openai");
        }

        return std::make_unique<OpenAiClient>(std::move(oai_config), transport);
    }

    if (provider == "claude") {
        ClaudeConfig claude_config;
        claude_config.api_key = get_env("ANTHROPIC_API_KEY");
        claude_config.model = model_override.empty() ? config.claude_model : model_override;

        if (claude_config.api_key.empty()) {
            throw std::runtime_error("ANTHROPIC_API_KEY environment variable is required for provider=claude");
        }

        return std::make_unique<ClaudeClient>(std::move(claude_config), transport);
    }

    if (provider == "gemini") {
        GeminiConfig gemini_config;
        gemini_config.api_key = get_env("GEMINI_API_KEY");
        gemini_config.model = model_override.empty() ? config.gemini_model : model_override;

        if (gemini_config.api_key.empty()) {
            throw std::runtime_error("GEMINI_API_KEY environment variable is required for provider=gemini");
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
            throw std::runtime_error("GROQ_API_KEY environment variable is required for provider=groq");
        }

        return std::make_unique<OpenAiClient>(std::move(groq_config), transport);
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
