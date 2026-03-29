#ifndef CORE_ROUTING_MODEL_ROUTER_H
#define CORE_ROUTING_MODEL_ROUTER_H

#include <memory>
#include <string>
#include <vector>

#include "../interfaces/model_client.h"
#include "../model/chat_message.h"
#include "../model/tool_schema.h"

/// Routes between a fast model (e.g., Groq/llama-3.3-70b at 300 tok/s)
/// and a strong model (e.g., Claude/GPT-4o) based on task complexity.
///
/// Strategy:
/// - Simple tool routing (read_file, list_dir, calculator) → fast model
/// - Complex reasoning, multi-step planning → strong model
/// - Tool result processing → fast model (just need to decide next action)
///
/// This is the key to Groq-level agent speed: use the fastest inference
/// for the 80% of turns that are simple routing decisions.
class ModelRouter : public IModelClient {
public:
    struct Config {
        bool enabled = true;
        int complexity_threshold = 100;  // tokens in user message before switching to strong
        int max_fast_tool_calls = 3;     // after N consecutive tool calls, switch to strong
    };

    ModelRouter(std::unique_ptr<IModelClient> fast_model,
                std::unique_ptr<IModelClient> strong_model);
    ModelRouter(std::unique_ptr<IModelClient> fast_model,
                std::unique_ptr<IModelClient> strong_model,
                Config config);

    std::string generate(const std::string& prompt) const override;
    const std::string& model() const override;

    ChatMessage chat(const std::vector<ChatMessage>& messages,
                     const std::vector<ToolSchema>& tools) const override;

    ChatMessage chat_streaming(const std::vector<ChatMessage>& messages,
                               const std::vector<ToolSchema>& tools,
                               TokenCallback on_token) const override;

    bool supports_tools() const override;
    bool supports_streaming() const override;

    /// Which model was used in the last call (for debugging/logging).
    const std::string& last_route() const { return last_route_; }

private:
    std::unique_ptr<IModelClient> fast_;
    std::unique_ptr<IModelClient> strong_;
    Config config_;
    mutable std::string last_route_;
    mutable int consecutive_tool_calls_ = 0;

    /// Decide which model to use based on conversation context.
    IModelClient& select(const std::vector<ChatMessage>& messages) const;

    /// Estimate complexity of the current turn.
    int estimate_complexity(const std::vector<ChatMessage>& messages) const;
};

#endif
