#ifndef CORE_MODEL_TOOL_SCHEMA_H
#define CORE_MODEL_TOOL_SCHEMA_H

#include <string>
#include <vector>

struct ToolParameter {
    std::string name;
    std::string type = "string";
    std::string description;
    bool required = false;
};

struct ToolSchema {
    std::string name;
    std::string description;
    std::vector<ToolParameter> parameters;
};

#endif
