#ifndef AGENT_TOOL_H
#define AGENT_TOOL_H

#include <string>

struct ToolResult {
    bool success;
    std::string content;
};

struct ToolPreview {
    std::string summary;
    std::string details;
};

class Tool {
public:
    virtual ~Tool() = default;

    virtual std::string name() const = 0;
    virtual std::string description() const = 0;
    virtual ToolPreview preview(const std::string& args) const {
        return {"", args};
    }
    virtual ToolResult run(const std::string& args) const = 0;
};

#endif
