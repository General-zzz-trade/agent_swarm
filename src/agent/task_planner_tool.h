#ifndef AGENT_TASK_PLANNER_TOOL_H
#define AGENT_TASK_PLANNER_TOOL_H

#include <string>
#include <vector>

#include "tool.h"

/// Task planner tool: lets the model decompose complex tasks into steps
/// and track progress as it executes them.
///
/// The model calls this tool to:
/// 1. Create a plan: "plan: <task description>" → generates numbered steps
/// 2. Mark a step done: "done: <step number>" → updates status
/// 3. View current plan: "status" → shows all steps with completion status
///
/// This gives the agent explicit "thinking" about multi-step tasks,
/// making it much more reliable at complex work. The plan is stored
/// in conversation history, so the model always sees its progress.
class TaskPlannerTool : public Tool {
public:
    std::string name() const override;
    std::string description() const override;
    ToolResult run(const std::string& args) const override;
    ToolSchema schema() const override;
    bool is_read_only() const override { return true; }

private:
    struct Step {
        int number;
        std::string description;
        bool completed;
    };

    mutable std::vector<Step> steps_;
    mutable std::string task_description_;

    ToolResult handle_plan(const std::string& task) const;
    ToolResult handle_done(const std::string& step_str) const;
    ToolResult handle_status() const;
    std::string format_plan() const;
};

#endif
