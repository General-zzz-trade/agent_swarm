#ifndef AGENT_MESSAGE_H
#define AGENT_MESSAGE_H

#include <string>
#include <vector>
#include "../core/model/chat_message.h"

struct Message {
    std::string role;
    std::string content;
    std::string name;
    std::string tool_call_id;
    std::string reasoning_content;  // For thinking models (kimi-k2.5, deepseek-r1)
    std::vector<ToolCallRequest> tool_calls;  // For assistant messages with tool calls
};

#endif
