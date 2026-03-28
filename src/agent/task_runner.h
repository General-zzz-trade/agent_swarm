#ifndef AGENT_TASK_RUNNER_H
#define AGENT_TASK_RUNNER_H

#include <functional>
#include <string>
#include <vector>

#include "action.h"
#include "execution_step.h"
#include "observation.h"
#include "permission_policy.h"
#include "tool_registry.h"

struct TaskStepResult {
    ExecutionStep step;
    Observation observation;
    bool should_return_reply = false;
    std::string reply;
};

class TaskRunner {
public:
    using DebugLogger =
        std::function<void(const std::string& title, const std::string& body)>;
    using AuditLogger = std::function<void(const std::string& stage,
                                           const std::string& tool_name,
                                           const std::string& target,
                                           const std::string& detail,
                                           bool approved)>;
    using ApprovalRequester =
        std::function<bool(const Action& action, const PolicyDecision& decision, const Tool& tool)>;
    using AuditTargetBuilder = std::function<std::string(const Action& action)>;

    TaskRunner(const PermissionPolicy& policy,
               const ToolRegistry& tools,
               DebugLogger debug_logger,
               AuditLogger audit_logger,
               ApprovalRequester approval_requester,
               AuditTargetBuilder audit_target_builder);

    TaskStepResult execute(const Action& action, std::size_t step_index) const;
    std::vector<TaskStepResult> execute_batch(const std::vector<Action>& actions,
                                               std::size_t start_index) const;

private:
    const PermissionPolicy& policy_;
    const ToolRegistry& tools_;
    DebugLogger debug_logger_;
    AuditLogger audit_logger_;
    ApprovalRequester approval_requester_;
    AuditTargetBuilder audit_target_builder_;
};

#endif
