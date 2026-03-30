#ifndef CORE_MODEL_CHAT_MESSAGE_H
#define CORE_MODEL_CHAT_MESSAGE_H

#include <functional>
#include <string>
#include <vector>

enum class ChatRole { system, user, assistant, tool };

inline const char* chat_role_to_string(ChatRole role) {
    switch (role) {
        case ChatRole::system: return "system";
        case ChatRole::user: return "user";
        case ChatRole::assistant: return "assistant";
        case ChatRole::tool: return "tool";
    }
    return "user";
}

struct ToolCallRequest {
    std::string id;
    std::string name;
    std::string arguments;  // JSON string
};

struct TokenUsage {
    int input_tokens = 0;
    int output_tokens = 0;
    int cache_creation_tokens = 0;
    int cache_read_tokens = 0;
};

struct ChatMessage {
    ChatRole role = ChatRole::user;
    std::string content;
    std::string name;            // for tool role: which tool produced this
    std::string tool_call_id;    // for tool role: which tool_call this responds to
    std::string reasoning_content;  // for thinking models (kimi-k2.5, deepseek-r1, o3)
    std::vector<ToolCallRequest> tool_calls;  // for assistant role: requested tool calls
    TokenUsage usage;  // token usage from API response

    bool has_tool_calls() const { return !tool_calls.empty(); }
};

// Callback for streaming tokens. Return false to abort.
using TokenCallback = std::function<bool(const std::string& token)>;

#endif
