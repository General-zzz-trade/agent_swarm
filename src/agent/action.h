#ifndef AGENT_ACTION_H
#define AGENT_ACTION_H

#include <string>

enum class ActionType {
    reply,
    tool,
};

struct Action {
    ActionType type;
    std::string tool_name;
    std::string content;
    std::string args;
    std::string reason;
    std::string risk;
    bool requires_confirmation = false;
};

#endif
