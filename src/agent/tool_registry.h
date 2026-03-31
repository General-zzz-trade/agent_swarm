#ifndef AGENT_TOOL_REGISTRY_H
#define AGENT_TOOL_REGISTRY_H

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "tool.h"

class ToolRegistry {
public:
    void register_tool(std::unique_ptr<Tool> tool);
    const Tool* find(const std::string& name) const;
    Tool* find_mutable(const std::string& name);
    std::vector<const Tool*> list() const;
    bool empty() const;

private:
    std::vector<std::unique_ptr<Tool>> tools_;
    std::unordered_map<std::string, Tool*> index_;  // O(1) lookup by name
};

#endif
