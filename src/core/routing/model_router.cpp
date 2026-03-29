#include "model_router.h"

#include <algorithm>
#include <stdexcept>

ModelRouter::ModelRouter(std::unique_ptr<IModelClient> fast_model,
                         std::unique_ptr<IModelClient> strong_model)
    : fast_(std::move(fast_model)),
      strong_(std::move(strong_model)),
      config_() {
    if (!fast_) throw std::invalid_argument("ModelRouter requires a fast model");
    if (!strong_) throw std::invalid_argument("ModelRouter requires a strong model");
}

ModelRouter::ModelRouter(std::unique_ptr<IModelClient> fast_model,
                         std::unique_ptr<IModelClient> strong_model,
                         Config config)
    : fast_(std::move(fast_model)),
      strong_(std::move(strong_model)),
      config_(std::move(config)) {
    if (!fast_) throw std::invalid_argument("ModelRouter requires a fast model");
    if (!strong_) throw std::invalid_argument("ModelRouter requires a strong model");
}

std::string ModelRouter::generate(const std::string& prompt) const {
    // Legacy path: always use strong model (text prompts are usually complex)
    last_route_ = "strong:" + strong_->model();
    return strong_->generate(prompt);
}

const std::string& ModelRouter::model() const {
    static const std::string name = "router";
    return name;
}

ChatMessage ModelRouter::chat(const std::vector<ChatMessage>& messages,
                               const std::vector<ToolSchema>& tools) const {
    IModelClient& chosen = select(messages);
    ChatMessage result = chosen.chat(messages, tools);

    // Track consecutive tool calls for escalation
    if (result.has_tool_calls()) {
        ++consecutive_tool_calls_;
    } else {
        consecutive_tool_calls_ = 0;
    }

    return result;
}

ChatMessage ModelRouter::chat_streaming(const std::vector<ChatMessage>& messages,
                                         const std::vector<ToolSchema>& tools,
                                         TokenCallback on_token) const {
    IModelClient& chosen = select(messages);
    ChatMessage result = chosen.chat_streaming(messages, tools, on_token);

    if (result.has_tool_calls()) {
        ++consecutive_tool_calls_;
    } else {
        consecutive_tool_calls_ = 0;
    }

    return result;
}

bool ModelRouter::supports_tools() const {
    return fast_->supports_tools() && strong_->supports_tools();
}

bool ModelRouter::supports_streaming() const {
    return fast_->supports_streaming() || strong_->supports_streaming();
}

IModelClient& ModelRouter::select(const std::vector<ChatMessage>& messages) const {
    if (!config_.enabled) {
        last_route_ = "strong:" + strong_->model();
        return *strong_;
    }

    const int complexity = estimate_complexity(messages);

    // Escalate to strong model if:
    // 1. High complexity (long user message, complex reasoning needed)
    // 2. Too many consecutive tool calls (might be stuck in a loop)
    // 3. First turn with no history (need good planning)
    const bool is_first_turn = messages.size() <= 2;  // system + user
    const bool high_complexity = complexity > config_.complexity_threshold;
    const bool too_many_tool_calls = consecutive_tool_calls_ >= config_.max_fast_tool_calls;

    if (is_first_turn || high_complexity || too_many_tool_calls) {
        last_route_ = "strong:" + strong_->model();
        if (too_many_tool_calls) {
            consecutive_tool_calls_ = 0;  // Reset after escalation
        }
        return *strong_;
    }

    // Simple routing turns: tool results → next action decision
    last_route_ = "fast:" + fast_->model();
    return *fast_;
}

int ModelRouter::estimate_complexity(const std::vector<ChatMessage>& messages) const {
    if (messages.empty()) return 0;

    int score = 0;

    // Find the last user message
    const ChatMessage* last_user = nullptr;
    for (auto it = messages.rbegin(); it != messages.rend(); ++it) {
        if (it->role == ChatRole::user) {
            last_user = &(*it);
            break;
        }
    }

    if (last_user) {
        // Length-based complexity
        score += static_cast<int>(last_user->content.size() / 4);  // ~tokens

        // Keyword-based complexity boost
        const std::string& text = last_user->content;
        if (text.find("explain") != std::string::npos) score += 30;
        if (text.find("analyze") != std::string::npos) score += 30;
        if (text.find("design") != std::string::npos) score += 40;
        if (text.find("refactor") != std::string::npos) score += 40;
        if (text.find("debug") != std::string::npos) score += 20;
        if (text.find("why") != std::string::npos) score += 15;
        if (text.find("how") != std::string::npos) score += 10;
    }

    // If the last message is a tool result, it's likely a simple routing decision
    if (!messages.empty() && messages.back().role == ChatRole::tool) {
        score = std::max(0, score - 50);
    }

    return score;
}
