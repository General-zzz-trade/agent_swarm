#include "task_runner.h"

#include <future>
#include <utility>

namespace {

void invoke_debug(const TaskRunner::DebugLogger& logger,
                  const std::string& title,
                  const std::string& body) {
    if (logger) {
        logger(title, body);
    }
}

void invoke_audit(const TaskRunner::AuditLogger& logger,
                  const std::string& stage,
                  const std::string& tool_name,
                  const std::string& target,
                  const std::string& detail,
                  bool approved) {
    if (logger) {
        logger(stage, tool_name, target, detail, approved);
    }
}

std::string build_policy_debug_body(const PolicyDecision& decision) {
    return "allowed=" + std::string(decision.allowed ? "true" : "false") +
           "\napproval_required=" +
           std::string(decision.approval_required ? "true" : "false") +
           "\nrisk=" + decision.effective_risk +
           "\nreason=" + decision.reason;
}

}  // namespace

TaskRunner::TaskRunner(const PermissionPolicy& policy,
                       const ToolRegistry& tools,
                       DebugLogger debug_logger,
                       AuditLogger audit_logger,
                       ApprovalRequester approval_requester,
                       AuditTargetBuilder audit_target_builder)
    : policy_(policy),
      tools_(tools),
      debug_logger_(std::move(debug_logger)),
      audit_logger_(std::move(audit_logger)),
      approval_requester_(std::move(approval_requester)),
      audit_target_builder_(std::move(audit_target_builder)) {}

TaskStepResult TaskRunner::execute(const Action& action, std::size_t step_index) const {
    TaskStepResult result;
    result.step.index = step_index;
    result.step.tool_name = action.tool_name;
    result.step.args = action.args;
    result.step.reason = action.reason;
    result.step.risk = action.risk;

    const PolicyDecision decision = policy_.evaluate(action);
    invoke_debug(debug_logger_, "Policy Decision", build_policy_debug_body(decision));

    const std::string audit_target =
        audit_target_builder_ ? audit_target_builder_(action) : action.args;
    if (decision.approval_required) {
        invoke_audit(audit_logger_, "requested", action.tool_name, audit_target,
                     "risk=" + decision.effective_risk + " reason=" + action.reason, false);
    }

    const Tool* tool = tools_.find(action.tool_name);
    if (tool == nullptr) {
        result.step.status = ExecutionStepStatus::blocked;
        result.step.detail = "Tool not found";
        result.observation = {false, "tool_lookup", "Tool not found: " + action.tool_name};
        invoke_debug(debug_logger_, "Tool Lookup", result.observation.content);
        return result;
    }

    bool allowed = decision.allowed;
    if (!allowed && decision.approval_required) {
        const bool approved =
            approval_requester_ ? approval_requester_(action, decision, *tool) : false;
        if (!approved) {
            result.step.status = ExecutionStepStatus::denied;
            result.step.detail = decision.reason;
            result.observation = {false, "approval",
                                  "User denied approval for tool: " + action.tool_name};
            result.should_return_reply = true;
            result.reply = "Approval denied for " + action.tool_name +
                           ". The action was not executed.";
            invoke_audit(audit_logger_, "approval_denied", action.tool_name, audit_target,
                         decision.reason, false);
            return result;
        }
        allowed = true;
        invoke_audit(audit_logger_, "approved", action.tool_name, audit_target, decision.reason,
                     true);
    }

    if (!allowed) {
        result.step.status = ExecutionStepStatus::blocked;
        result.step.detail = decision.reason;
        result.observation = {false, "policy", decision.reason};
        if (decision.approval_required) {
            invoke_audit(audit_logger_, "policy_denied", action.tool_name, audit_target,
                         decision.reason, false);
        }
        return result;
    }

    invoke_debug(debug_logger_, "Tool Call",
                 "tool=" + tool->name() + "\nargs=" + action.args);
    const ToolResult tool_result = tool->run(action.args);
    invoke_debug(debug_logger_, "Tool Result",
                 std::string("tool=") + tool->name() +
                     "\nsuccess=" + (tool_result.success ? "true" : "false") +
                     "\ncontent=" + tool_result.content);

    result.step.status =
        tool_result.success ? ExecutionStepStatus::completed : ExecutionStepStatus::failed;
    result.step.detail = tool_result.content;
    result.observation = {tool_result.success, "tool", tool_result.content};
    return result;
}

std::vector<TaskStepResult> TaskRunner::execute_batch(
    const std::vector<Action>& actions, std::size_t start_index) const {

    if (actions.empty()) {
        return {};
    }

    if (actions.size() == 1) {
        return {execute(actions[0], start_index)};
    }

    // Check if all tools are read-only (safe for parallel execution)
    bool all_read_only = true;
    for (const auto& action : actions) {
        const Tool* tool = tools_.find(action.tool_name);
        if (tool == nullptr || !tool->is_read_only()) {
            all_read_only = false;
            break;
        }
    }

    if (all_read_only) {
        // Execute in parallel
        std::vector<std::future<TaskStepResult>> futures;
        futures.reserve(actions.size());
        for (std::size_t i = 0; i < actions.size(); ++i) {
            const std::size_t idx = start_index + i;
            futures.push_back(std::async(std::launch::async,
                [this, &actions, i, idx]() {
                    return execute(actions[i], idx);
                }));
        }

        std::vector<TaskStepResult> results;
        results.reserve(futures.size());
        for (auto& f : futures) {
            results.push_back(f.get());
        }
        return results;
    }

    // Sequential execution for write tools
    std::vector<TaskStepResult> results;
    results.reserve(actions.size());
    for (std::size_t i = 0; i < actions.size(); ++i) {
        results.push_back(execute(actions[i], start_index + i));
    }
    return results;
}
