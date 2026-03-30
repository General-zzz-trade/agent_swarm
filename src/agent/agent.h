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
#include "../core/model/chat_message.h"
#include "../core/model/tool_schema.h"
#include "../core/threading/thread_pool.h"
#include "../core/indexing/file_index.h"
#include "../core/indexing/file_prefetch.h"
#include "../core/routing/prompt_compressor.h"
#include "../core/caching/tool_result_cache.h"
#include "../core/session/memory_store.h"
#include "action.h"
#include "execution_step.h"
#include "failure_tracker.h"
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

    using StreamCallback = std::function<void(const std::string& token)>;

    std::string run_turn(const std::string& user_input);
    std::string run_turn_streaming(const std::string& user_input, StreamCallback on_token);
    void clear_history();
    const std::string& model() const;
    bool debug_enabled() const;
    const std::vector<ExecutionStep>& last_execution_trace() const;
    void set_trace_observer(TraceObserver observer);
    std::vector<std::string> available_tool_names() const;
    ToolResult run_diagnostic_tool(const std::string& tool_name, const std::string& args) const;

    /// Access to the thread pool for external components.
    ThreadPool& thread_pool() { return thread_pool_; }

    /// Access to the file index for search tools.
    const FileIndex& file_index() const { return file_index_; }

    /// Access to the prefetch cache for tools that read files.
    FilePrefetchCache& prefetch_cache() { return prefetch_cache_; }

    /// Access to memory stores for persistent cross-session memory.
    MemoryStore& global_memory() { return global_memory_; }
    MemoryStore& workspace_memory() { return workspace_memory_; }

    // History access for session save/load
    std::vector<ChatMessage> get_chat_messages() const;
    void restore_history(const std::vector<ChatMessage>& messages);

    // Compact context
    void compact_history();

    // Debug toggle
    void set_debug(bool enabled);

    // Cancellation support
    void set_cancellation_check(std::function<bool()> check);

    // Last response token usage
    TokenUsage last_token_usage() const;

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

    int auto_verify_count_ = 0;
    FailureTracker failure_tracker_;
    ThreadPool thread_pool_;
    FileIndex file_index_;
    FilePrefetchCache prefetch_cache_;
    PromptCompressor prompt_compressor_;
    ToolResultCache tool_result_cache_;
    MemoryStore global_memory_;
    MemoryStore workspace_memory_;
    std::function<bool()> cancellation_check_;
    TokenUsage last_usage_;

    std::string build_prompt() const;
    std::vector<ChatMessage> build_chat_messages() const;
    std::vector<ToolSchema> build_tool_schemas() const;
    std::string run_turn_structured(const std::string& user_input,
                                     StreamCallback on_token = nullptr);
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
