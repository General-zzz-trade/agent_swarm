#ifndef AGENT_TOOL_H
#define AGENT_TOOL_H

#include <string>
#include <vector>

#include "../core/model/tool_schema.h"

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

    // Schema for structured function calling. Default: single "args" string parameter.
    virtual ToolSchema schema() const {
        return {name(), description(), {{"args", "string", "Tool arguments", false}}};
    }

    // Whether this tool only reads state (safe for parallel execution).
    virtual bool is_read_only() const { return false; }
};

#endif
