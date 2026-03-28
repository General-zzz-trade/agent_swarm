#include "tool_registry.h"

#include <algorithm>
#include <stdexcept>

void ToolRegistry::register_tool(std::unique_ptr<Tool> tool) {
    if (tool == nullptr) {
        throw std::invalid_argument("Cannot register a null tool");
    }

    const std::string tool_name = tool->name();
    if (find(tool_name) != nullptr) {
        throw std::invalid_argument("Tool already registered: " + tool_name);
    }

    tools_.push_back(std::move(tool));
}

const Tool* ToolRegistry::find(const std::string& name) const {
    auto it = std::find_if(tools_.begin(), tools_.end(),
                           [&name](const std::unique_ptr<Tool>& tool) {
                               return tool->name() == name;
                           });
    return it == tools_.end() ? nullptr : it->get();
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
