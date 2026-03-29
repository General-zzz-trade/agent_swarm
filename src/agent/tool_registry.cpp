#include "tool_registry.h"

#include <algorithm>
#include <stdexcept>

void ToolRegistry::register_tool(std::unique_ptr<Tool> tool) {
    if (tool == nullptr) {
        throw std::invalid_argument("Cannot register a null tool");
    }

    const std::string tool_name = tool->name();
    if (index_.count(tool_name) > 0) {
        throw std::invalid_argument("Tool already registered: " + tool_name);
    }

    Tool* raw = tool.get();
    tools_.push_back(std::move(tool));
    index_.emplace(tool_name, raw);
}

const Tool* ToolRegistry::find(const std::string& name) const {
    const auto it = index_.find(name);
    return it == index_.end() ? nullptr : it->second;
}

std::vector<const Tool*> ToolRegistry::list() const {
    std::vector<const Tool*> result;
    result.reserve(tools_.size());
    for (const auto& tool : tools_) {
        result.push_back(tool.get());
    }
    return result;
}

bool ToolRegistry::empty() const {
    return tools_.empty();
}
