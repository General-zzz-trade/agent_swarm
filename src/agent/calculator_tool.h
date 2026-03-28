#ifndef AGENT_CALCULATOR_TOOL_H
#define AGENT_CALCULATOR_TOOL_H

#include "tool.h"

class CalculatorTool : public Tool {
public:
    std::string name() const override;
    std::string description() const override;
    ToolResult run(const std::string& args) const override;
};

#endif
