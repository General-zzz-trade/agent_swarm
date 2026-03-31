#ifndef AGENT_TASK_PLANNER_TOOL_H
#define AGENT_TASK_PLANNER_TOOL_H

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "tool.h"

class Agent;
class SwarmCoordinator;
class ThreadPool;

/// Task planner tool: lets the model decompose complex tasks into steps
/// and track progress as it executes them.
///
/// The model calls this tool to:
/// 1. Create a plan: "plan: <task description>" → generates numbered steps
/// 2. Mark a step done: "done: <step number>" → updates status
/// 3. View current plan: "status" → shows all steps with completion status
/// 4. Parallel execution: "parallel: <task1>\n<task2>" → run sub-tasks concurrently
///
/// This gives the agent explicit "thinking" about multi-step tasks,
/// making it much more reliable at complex work. The plan is stored
/// in conversation history, so the model always sees its progress.
class TaskPlannerTool : public Tool {
public:
    using AgentFactory = std::function<std::unique_ptr<Agent>()>;

    TaskPlannerTool();
    ~TaskPlannerTool() override;

    /// Enable parallel execution support via swarm coordinator.
    void set_swarm_support(std::shared_ptr<ThreadPool> pool, AgentFactory factory);

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
    mutable std::shared_ptr<ThreadPool> swarm_pool_;
    mutable std::unique_ptr<SwarmCoordinator> swarm_;

    ToolResult handle_plan(const std::string& task) const;
    ToolResult handle_done(const std::string& step_str) const;
    ToolResult handle_status() const;
    ToolResult handle_parallel(const std::string& tasks_text) const;
    std::string format_plan() const;
};

#endif
