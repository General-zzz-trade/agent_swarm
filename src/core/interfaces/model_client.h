#ifndef CORE_INTERFACES_MODEL_CLIENT_H
#define CORE_INTERFACES_MODEL_CLIENT_H

#include <string>
#include <vector>

#include "../model/chat_message.h"
#include "../model/tool_schema.h"

class IModelClient {
public:
    virtual ~IModelClient() = default;

    // Legacy text-prompt interface (kept for backward compatibility)
    virtual std::string generate(const std::string& prompt) const = 0;
    virtual const std::string& model() const = 0;

    // Structured chat interface with tool calling support.
    // Default: flattens messages to text, calls generate(), wraps result.
    virtual ChatMessage chat(const std::vector<ChatMessage>& messages,
                             const std::vector<ToolSchema>& tools) const;

    // Streaming chat: invokes on_token for each incremental token.
    // Default: calls chat() and fires callback once with full content.
    virtual ChatMessage chat_streaming(const std::vector<ChatMessage>& messages,
                                       const std::vector<ToolSchema>& tools,
                                       TokenCallback on_token) const;

    // Feature queries
    virtual bool supports_tools() const { return false; }
    virtual bool supports_streaming() const { return false; }
};

// --- Default inline implementations ---

inline ChatMessage IModelClient::chat(const std::vector<ChatMessage>& messages,
                                      const std::vector<ToolSchema>& /*tools*/) const {
    // Flatten messages into a single text prompt (legacy behavior)
    std::string prompt;
    for (const auto& msg : messages) {
        if (msg.role == ChatRole::system) {
            prompt += msg.content + "\n\n";
        } else if (msg.role == ChatRole::user) {
            prompt += "[user]\n" + msg.content + "\n\n";
        } else if (msg.role == ChatRole::assistant) {
            prompt += "[assistant]\n" + msg.content + "\n\n";
        } else if (msg.role == ChatRole::tool) {
            prompt += "[tool:" + msg.name + "]\n" + msg.content + "\n\n";
        }
    }
    prompt += "Decide the next action now.\n";

    const std::string response = generate(prompt);

    ChatMessage result;
    result.role = ChatRole::assistant;
    result.content = response;
    return result;
}

inline ChatMessage IModelClient::chat_streaming(const std::vector<ChatMessage>& messages,
                                                 const std::vector<ToolSchema>& tools,
                                                 TokenCallback on_token) const {
    ChatMessage result = chat(messages, tools);
    if (on_token) {
        on_token(result.content);
    }
    return result;
}

#endif
