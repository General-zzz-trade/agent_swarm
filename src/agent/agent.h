#ifndef AGENT_AGENT_H
#define AGENT_AGENT_H

#include <filesystem>
#include <functional>
#include <iosfwd>
#include <memory>
#include <string>
#include <vector>

#include "../core/interfaces/audit_logger.h"
#include "../core/interfaces/command_runner.h"
#include "../core/config/agent_runtime_config.h"
#include "../core/config/command_policy_config.h"
#include "../core/config/policy_config.h"
#include "../core/interfaces/file_system.h"
#include "../core/interfaces/model_client.h"
#include "../core/interfaces/approval_provider.h"
#include "action.h"
#include "execution_step.h"
#include "message.h"
#include "permission_policy.h"
#include "task_runner.h"
#include "tool_set_factory.h"
#include "tool_registry.h"

class Agent {
public:
    using TraceObserver = std::function<void(const std::vector<ExecutionStep>& trace)>;

    Agent(std::unique_ptr<IModelClient> client,
          std::shared_ptr<IApprovalProvider> approval_provider,
          std::filesystem::path workspace_root,
          PolicyConfig policy_config = {},
          AgentRuntimeConfig runtime_config = {},
          bool debug = false,
          std::shared_ptr<IAuditLogger> audit_logger = nullptr,
          ToolRegistry tools = {});

    std::string run_turn(const std::string& user_input);
    void clear_history();
    const std::string& model() const;
    bool debug_enabled() const;
    const std::vector<ExecutionStep>& last_execution_trace() const;
    void set_trace_observer(TraceObserver observer);
    std::vector<std::string> available_tool_names() const;
    ToolResult run_diagnostic_tool(const std::string& tool_name, const std::string& args) const;

private:
    std::unique_ptr<IModelClient> client_;
    std::shared_ptr<IApprovalProvider> approval_provider_;
    std::shared_ptr<IAuditLogger> audit_logger_;
    PermissionPolicy policy_;
    ToolRegistry tools_;
    std::vector<Message> history_;
    std::vector<ExecutionStep> last_execution_trace_;
    std::filesystem::path workspace_root_;
    AgentRuntimeConfig runtime_config_;
    bool debug_;
    TraceObserver trace_observer_;

    std::string build_prompt() const;
    void push_history(Message message);
    void enforce_history_budget();
    void notify_trace_updated() const;
    void log_tool_audit(const std::string& stage,
                        const std::string& tool_name,
                        const std::string& target,
                        const std::string& detail,
                        bool approved = false) const;
    bool request_approval(const Action& action,
                          const PolicyDecision& decision,
                          const Tool& tool);
    void log_debug(const std::string& title, const std::string& body) const;
};

#endif
