#include "agent.h"

#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <unordered_set>
#include <utility>

#include <nlohmann/json.hpp>

#include "action_parser.h"
#include "skill_loader.h"
#include "workspace_prompt.h"
#include "../core/routing/model_router.h"

namespace {

std::string summarize_message(const Message& message) {
    std::ostringstream output;
    if (message.role == "tool") {
        output << "[tool:" << message.name << "]\n";
    } else {
        output << "[" << message.role << "]\n";
    }
    output << message.content << "\n";
    return output.str();
}

std::size_t estimate_message_size(const Message& message) {
    std::size_t size =
        message.role.size() + message.name.size() + message.tool_call_id.size() +
        message.content.size() + 32;
    for (const auto& tool_call : message.tool_calls) {
        size += tool_call.id.size() + tool_call.name.size() + tool_call.arguments.size();
    }
    return size;
}

std::string trim_copy(const std::string& value) {
    const auto begin = value.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) {
        return "";
    }
    const auto end = value.find_last_not_of(" \t\r\n");
    return value.substr(begin, end - begin + 1);
}

std::string shorten_value(const std::string& value, std::size_t max_length) {
    if (value.size() <= max_length) {
        return value;
    }
    return value.substr(0, max_length) + "...";
}

std::string extract_path_argument(const std::string& args) {
    const std::size_t line_end = args.find('\n');
    const std::string first_line = line_end == std::string::npos ? args : args.substr(0, line_end);
    if (first_line.rfind("path=", 0) == 0 || first_line.rfind("path:", 0) == 0) {
        return trim_copy(first_line.substr(5));
    }
    return "";
}

std::string audit_target_for_action(const Action& action) {
    if (action.tool_name == "write_file" || action.tool_name == "edit_file") {
        const std::string path = extract_path_argument(action.args);
        return path.empty() ? "[unparsed path]" : path;
    }
    if (action.tool_name == "focus_window") {
        const std::string trimmed = trim_copy(action.args);
        if (trimmed.rfind("title=", 0) == 0 || trimmed.rfind("title:", 0) == 0 ||
            trimmed.rfind("handle=", 0) == 0 || trimmed.rfind("handle:", 0) == 0) {
            return shorten_value(trimmed, 160);
        }
        return "title=" + shorten_value(trimmed, 160);
    }
    if (action.tool_name == "click_element") {
        return shorten_value(trim_copy(action.args), 160);
    }
    if (action.tool_name == "type_text") {
        return "chars=" + std::to_string(action.args.size());
    }
    return shorten_value(trim_copy(action.args), 160);
}

bool detect_small_model(const std::string& model_name) {
    // Auto-detect small/budget models that benefit from compact prompts
    static const std::vector<std::string> small_model_hints = {
        "8k", "lite", "mini", "flash", "turbo", "instant", "tiny", "small", "nano"
    };
    for (const auto& hint : small_model_hints) {
        if (model_name.find(hint) != std::string::npos) {
            return true;
        }
    }
    return false;
}

void request_router_failure_escalation(const std::unique_ptr<IModelClient>& client) {
    if (client == nullptr) {
        return;
    }
    if (auto* router = dynamic_cast<ModelRouter*>(client.get())) {
        router->request_failure_escalation();
    }
}

}  // namespace

Agent::Agent(std::unique_ptr<IModelClient> client,
             std::shared_ptr<IApprovalProvider> approval_provider,
             std::filesystem::path workspace_root,
             PolicyConfig policy_config,
             AgentRuntimeConfig runtime_config,
             bool debug,
             std::shared_ptr<IAuditLogger> audit_logger,
             ToolRegistry tools)
    : client_(std::move(client)),
      approval_provider_(std::move(approval_provider)),
      audit_logger_(std::move(audit_logger)),
      policy_(std::move(policy_config)),
      tools_(std::move(tools)),
      workspace_root_(std::filesystem::weakly_canonical(std::move(workspace_root))),
      runtime_config_(std::move(runtime_config)),
      debug_(debug),
      failure_tracker_(runtime_config_.max_consecutive_failures),
      thread_pool_(std::max(2u, std::thread::hardware_concurrency())),
      prefetch_cache_(thread_pool_),
      speculative_executor_(tools_, thread_pool_) {
    if (client_ == nullptr) {
        throw std::invalid_argument("Agent requires a model client");
    }
    if (approval_provider_ == nullptr) {
        throw std::invalid_argument("Agent requires an approval provider");
    }
    if (tools_.empty()) {
        throw std::invalid_argument("Agent requires a tool registry");
    }

    // Initialize memory stores
    const char* home = std::getenv("HOME");
    if (home) {
        global_memory_ = MemoryStore(std::filesystem::path(home) / ".bolt" / "memory.json");
        global_memory_.load();
    }
    workspace_memory_ = MemoryStore(workspace_root_ / ".bolt" / "memory.json");
    workspace_memory_.load();

    // Build file index asynchronously in background
    file_index_task_ = thread_pool_.submit([this]() {
        file_index_.build(workspace_root_);
        log_debug("FileIndex", "Indexed " + std::to_string(file_index_.file_count()) +
                  " files, " + std::to_string(file_index_.total_bytes()) + " bytes");
    });
}

Agent::~Agent() {
    if (file_index_task_.valid()) {
        file_index_task_.wait();
    }
}

std::string Agent::run_turn(const std::string& user_input) {
    if (client_->supports_tools()) {
        return run_turn_structured(user_input);
    }

    last_execution_trace_.clear();
    notify_trace_updated();
    push_history({"user", user_input, ""});

    TaskRunner task_runner(
        policy_, tools_,
        [this](const std::string& title, const std::string& body) { log_debug(title, body); },
        [this](const std::string& stage, const std::string& tool_name, const std::string& target,
               const std::string& detail, bool approved) {
            log_tool_audit(stage, tool_name, target, detail, approved);
        },
        [this](const Action& action, const PolicyDecision& decision, const Tool& tool) {
            return request_approval(action, decision, tool);
        },
        [](const Action& action) { return audit_target_for_action(action); },
        &thread_pool_);

    for (int step = 0; step < runtime_config_.max_model_steps; ++step) {
        const std::string prompt = build_prompt();
        log_debug("Prompt", prompt);
        const std::string model_output = client_->generate(prompt);
        log_debug("Model Output", model_output);

        Action action{ActionType::reply, "", model_output, ""};
        try {
            action = parse_action_response(model_output);
        } catch (const std::exception& e) {
            log_debug("Parse Error", e.what());
            push_history(
                {"system",
                 std::string("FORMAT_ERROR: ") + e.what() +
                      ". Return exactly one JSON object next.",
                 ""});
            continue;
        }

        if (action.type == ActionType::reply) {
            log_debug("Parsed Action",
                      "type=reply\ncontent=" + action.content +
                          "\nreason=" + action.reason +
                          "\nrisk=" + action.risk +
                          "\nrequires_confirmation=" +
                          (action.requires_confirmation ? "true" : "false"));
        } else {
            log_debug("Parsed Action",
                      "type=tool\ntool=" + action.tool_name +
                          "\nargs=" + action.args +
                          "\nreason=" + action.reason +
                          "\nrisk=" + action.risk +
                          "\nrequires_confirmation=" +
                          (action.requires_confirmation ? "true" : "false"));
        }

        if (action.type == ActionType::reply) {
            push_history({"assistant", action.content, ""});
            return action.content;
        }

        ExecutionStep active_step;
        active_step.index = last_execution_trace_.size() + 1;
        active_step.tool_name = action.tool_name;
        active_step.args = action.args;
        active_step.reason = action.reason;
        active_step.risk = action.risk;
        active_step.status = ExecutionStepStatus::planned;
        active_step.detail = "Pending execution";
        last_execution_trace_.push_back(active_step);
        notify_trace_updated();

        const TaskStepResult step_result = task_runner.execute(action, active_step.index);
        last_execution_trace_.back() = step_result.step;
        notify_trace_updated();

        std::string history_content;
        std::string history_name = action.tool_name;
        if (step_result.observation.channel == "tool") {
            history_content =
                std::string(step_result.observation.success ? "TOOL_OK\n" : "TOOL_ERROR\n") +
                step_result.observation.content;
        } else if (step_result.observation.channel == "policy") {
            history_content = "POLICY_DENY\n" + step_result.observation.content;
            history_name = "policy";
        } else if (step_result.observation.channel == "approval") {
            history_content = "APPROVAL_DENY\n" + step_result.observation.content;
            history_name = "approval";
        } else {
            history_content = step_result.observation.content;
        }

        push_history({"tool", history_content, history_name});
        if (step_result.should_return_reply) {
            push_history({"assistant", step_result.reply, ""});
            return step_result.reply;
        }
    }

    throw std::runtime_error("Agent exceeded the maximum number of model steps");
}

std::string Agent::run_turn_streaming(const std::string& user_input, StreamCallback on_token) {
    if (client_->supports_tools() && client_->supports_streaming()) {
        return run_turn_structured(user_input, on_token);
    }
    // Fall back to non-streaming
    std::string result = run_turn(user_input);
    if (on_token) on_token(result);
    return result;
}

void Agent::clear_history() {
    history_.clear();
    last_execution_trace_.clear();
    notify_trace_updated();
}

const std::string& Agent::model() const {
    return client_->model();
}

bool Agent::debug_enabled() const {
    return debug_;
}

const std::vector<ExecutionStep>& Agent::last_execution_trace() const {
    return last_execution_trace_;
}

void Agent::set_trace_observer(TraceObserver observer) {
    trace_observer_ = std::move(observer);
}

std::vector<std::string> Agent::available_tool_names() const {
    const std::vector<const Tool*> tools = tools_.list();
    std::vector<std::string> names;
    names.reserve(tools.size());
    for (const Tool* tool : tools) {
        names.push_back(tool->name());
    }
    return names;
}

ToolResult Agent::run_diagnostic_tool(const std::string& tool_name, const std::string& args) const {
    const Tool* tool = tools_.find(tool_name);
    if (tool == nullptr) {
        return {false, "Tool not registered: " + tool_name};
    }

    Action action{ActionType::tool, tool_name, "", args, "self-check", "low", false};
    const PolicyDecision decision = policy_.evaluate(action);
    if (!decision.allowed || decision.approval_required) {
        return {false, "Self-check cannot run " + tool_name + ": " + decision.reason};
    }

    try {
        return tool->run(args);
    } catch (const std::exception& error) {
        return {false, error.what()};
    }
}

std::vector<ChatMessage> Agent::get_chat_messages() const {
    std::vector<ChatMessage> messages;
    for (const auto& msg : history_) {
        ChatMessage cm;
        if (msg.role == "user") cm.role = ChatRole::user;
        else if (msg.role == "assistant") cm.role = ChatRole::assistant;
        else if (msg.role == "tool") {
            cm.role = ChatRole::tool;
            cm.name = msg.name;
            cm.tool_call_id = msg.tool_call_id;
        }
        else cm.role = ChatRole::user;
        cm.content = msg.content;
        cm.tool_calls = msg.tool_calls;
        messages.push_back(std::move(cm));
    }
    return messages;
}

void Agent::restore_history(const std::vector<ChatMessage>& messages) {
    history_.clear();
    for (const auto& cm : messages) {
        Message msg;
        msg.role = chat_role_to_string(cm.role);
        msg.content = cm.content;
        msg.name = cm.name;
        msg.tool_call_id = cm.tool_call_id;
        msg.tool_calls = cm.tool_calls;
        history_.push_back(std::move(msg));
    }
    last_execution_trace_.clear();
}

void Agent::compact_history() {
    auto messages = build_chat_messages();
    // build_chat_messages already runs compression
    // Replace history with compressed version
    history_.clear();
    for (const auto& cm : messages) {
        if (cm.role == ChatRole::system) continue; // skip system prompt
        Message msg;
        msg.role = chat_role_to_string(cm.role);
        msg.content = cm.content;
        msg.name = cm.name;
        msg.tool_call_id = cm.tool_call_id;
        msg.tool_calls = cm.tool_calls;
        history_.push_back(std::move(msg));
    }
}

void Agent::set_debug(bool enabled) {
    debug_ = enabled;
}

void Agent::set_cancellation_check(std::function<bool()> check) {
    cancellation_check_ = std::move(check);
}

TokenUsage Agent::last_token_usage() const {
    return last_usage_;
}

void Agent::push_history(Message message) {
    history_.push_back(std::move(message));
    enforce_history_budget();
}

void Agent::enforce_history_budget() {
    // Helper: check if removing the front message would break a tool call chain
    // (assistant with tool_calls must be followed by tool results)
    auto safe_to_remove_front = [this]() -> bool {
        if (history_.empty()) return false;
        const auto& front = history_.front();
        // If front is an assistant with tool_calls, we must also remove
        // all subsequent tool results that belong to it
        if (front.role == "assistant" && !front.tool_calls.empty()) {
            // Count how many tool results follow
            std::size_t tool_count = 0;
            for (std::size_t i = 1; i < history_.size(); ++i) {
                if (history_[i].role == "tool") ++tool_count;
                else break;
            }
            // Remove assistant + all its tool results together
            for (std::size_t i = 0; i <= tool_count && !history_.empty(); ++i) {
                history_.erase(history_.begin());
            }
            return true;
        }
        // Don't remove a tool result orphan (its assistant was already removed)
        // Just remove it
        return true;
    };

    while (history_.size() > runtime_config_.history_window) {
        if (!safe_to_remove_front()) break;
        if (history_.empty()) break;
        if (history_.front().role != "assistant" || history_.front().tool_calls.empty()) {
            history_.erase(history_.begin());
        }
        // If safe_to_remove_front already erased, the loop continues
    }

    std::size_t total_bytes = 0;
    for (const Message& message : history_) {
        total_bytes += estimate_message_size(message);
    }

    while (history_.size() > 1 && total_bytes > runtime_config_.history_byte_budget) {
        total_bytes -= estimate_message_size(history_.front());
        history_.erase(history_.begin());
    }
}

void Agent::notify_trace_updated() const {
    if (trace_observer_) {
        trace_observer_(last_execution_trace_);
    }
}

void Agent::log_tool_audit(const std::string& stage,
                           const std::string& tool_name,
                           const std::string& target,
                           const std::string& detail,
                           bool approved) const {
    if (audit_logger_ == nullptr) {
        return;
    }

    try {
        audit_logger_->log({"tool", stage, tool_name, target, workspace_root_.string(), 0, -1,
                            approved, false, false, detail});
    } catch (const std::exception& error) {
        log_debug("Audit Logger Error", error.what());
    }
}

std::string Agent::build_prompt() const {
    const bool use_compact = runtime_config_.compact_prompt ||
        detect_small_model(client_->model());

    std::ostringstream prompt;

    if (use_compact) {
        // Compact prompt for small models (~200 tokens instead of ~800)
        prompt << "You are a coding agent. Workspace: " << workspace_root_.string() << "\n";
        prompt << "Read code before editing. Verify with build_and_test after changes. Be concise.\n\n";
        prompt << "Tools: ";
        bool first = true;
        for (const Tool* tool : tools_.list()) {
            if (!first) prompt << ", ";
            prompt << tool->name();
            first = false;
        }
        prompt << "\n";
        prompt << "edit_file: path=<file> old<<<text>>> new<<<text>>>\n";
        prompt << "write_file: path=<file> content<<<text>>>\n";
    } else {
        prompt << "You are a local C++ coding agent running inside a workspace.\n";
        prompt << "Workspace root: " << workspace_root_.string() << "\n\n";
        prompt << "Available tools:\n";
        for (const Tool* tool : tools_.list()) {
            prompt << "- " << tool->name() << ": " << tool->description() << "\n";
        }

        prompt << "\nDecision rules:\n";
        prompt << "- If the latest user request asks for arithmetic or an exact calculation, call calculator before replying.\n";
        prompt << "- If the latest user request asks which files or directories exist, call list_dir.\n";
        prompt << "- If the latest user request asks where code or text appears, call search_code.\n";
        prompt << "- If the latest user request asks about file contents, call read_file after you know the path.\n";
        prompt << "- Use edit_file for targeted changes to an existing workspace file when you know one or more exact text blocks to replace, or one or more line ranges to rewrite.\n";
        prompt << "- edit_file requires user approval before execution.\n";
        prompt << "- Use write_file only when the user explicitly asks to create or replace a file inside the workspace.\n";
        prompt << "- write_file requires user approval before execution.\n";
        prompt << "- When the user specifies exact file content, preserve that text exactly. Do not shorten, paraphrase, or clean it up.\n";
        prompt << "- Use run_command only for safe developer commands such as git status, git diff, g++ --version, ctest, cmake --build, or ollama list.\n";
        prompt << "- run_command requires user approval before execution.\n";
        if (tools_.find("list_processes") != nullptr) {
            prompt << "- If the user asks which local applications are running, call list_processes.\n";
        }
        if (tools_.find("list_windows") != nullptr) {
            prompt << "- If the user asks which visible windows exist, call list_windows.\n";
        }
        if (tools_.find("open_app") != nullptr) {
            prompt << "- Use open_app only when the user explicitly asks to launch a local application.\n";
            prompt << "- open_app requires user approval before execution.\n";
        }
        if (tools_.find("focus_window") != nullptr) {
            prompt << "- Use focus_window only when the user explicitly asks to bring a visible window to the foreground.\n";
            prompt << "- focus_window requires user approval before execution.\n";
        }
        if (tools_.find("wait_for_window") != nullptr) {
            prompt << "- If the user asks you to wait for an application window to appear, call wait_for_window.\n";
        }
        if (tools_.find("inspect_ui") != nullptr) {
            prompt << "- If the user asks what controls or visible elements exist in the current window, call inspect_ui.\n";
        }
        if (tools_.find("click_element") != nullptr) {
            prompt << "- Use click_element only when the user explicitly asks you to click or focus a UI element.\n";
            prompt << "- click_element requires user approval before execution.\n";
        }
        if (tools_.find("type_text") != nullptr) {
            prompt << "- Use type_text only after the intended window is focused.\n";
            prompt << "- type_text requires user approval before execution.\n";
        }
        if (tools_.find("open_app") != nullptr && tools_.find("wait_for_window") != nullptr &&
            tools_.find("type_text") != nullptr) {
            prompt << "- For desktop tasks such as opening Notepad and entering text, prefer a multi-step sequence: open_app, wait_for_window, focus_window, type_text, then reply.\n";
        }
        if (tools_.find("inspect_ui") != nullptr && tools_.find("click_element") != nullptr) {
            prompt << "- For desktop UI tasks such as clicking a button or selecting an input, first inspect_ui, then click_element, then type_text if needed.\n";
        }
        prompt << "- If a tool already gives the needed facts, reply directly.\n";
        prompt << "- Never invent file contents.\n";
        prompt << "- Return JSON only.\n";

        prompt << "\nReturn exactly one JSON object and nothing else.\n";
        prompt << "Reply example:\n";
        prompt << "{\"action\":\"reply\",\"content\":\"The answer goes here.\",\"reason\":\"No tool is needed.\",\"risk\":\"low\",\"requires_confirmation\":\"false\"}\n";
        prompt << "Tool example:\n";
        prompt << "{\"action\":\"tool\",\"tool\":\"read_file\",\"args\":\"src/main.cpp\",\"reason\":\"Need to inspect the file before answering.\",\"risk\":\"low\",\"requires_confirmation\":\"false\"}\n";
        prompt << "Approval-required tool example:\n";
        prompt << "{\"action\":\"tool\",\"tool\":\"run_command\",\"args\":\"ollama list\",\"reason\":\"Need local model state before answering.\",\"risk\":\"medium\",\"requires_confirmation\":\"true\"}\n";
        prompt << "The args field must always be a JSON string.\n";
        prompt << "For edit_file, args must be either: path=<relative path> then one or more old<<< exact old text >>> new<<< replacement text >>> pairs; or path=<relative path> then one or more replace_lines=start-end content<<< replacement text >>> blocks. Do not mix the two modes.\n";
        prompt << "For write_file, args must be: path=<relative path> then a newline, then content<<<, then file content, then >>> on its own line.\n";
        prompt << "For run_command, args must be the exact command line string.\n";
        prompt << "For wait_for_window, args should be either a window title substring, or lines such as title=<text> and optional timeout_ms=<milliseconds>.\n";
        prompt << "For inspect_ui, args may be empty, or may include window_handle=<handle> and optional max_elements=<count>.\n";
        prompt << "For click_element, args should be handle=<element handle> or one or more selector lines such as text=<visible text> and class=<class name>.\n";
        prompt << "For type_text, args must be: text<<<, then the exact text to type, then >>> on its own line.\n";
    }

    prompt << "\nConversation so far:\n";
    const std::size_t start = history_.size() > runtime_config_.history_window
                                  ? history_.size() - runtime_config_.history_window
                                  : 0;
    for (std::size_t i = start; i < history_.size(); ++i) {
        prompt << summarize_message(history_[i]) << "\n";
    }

    prompt << "Decide the next action now.\n";
    return prompt.str();
}

bool Agent::request_approval(const Action& action,
                             const PolicyDecision& decision,
                             const Tool& tool) {
    const ToolPreview preview = tool.preview(action.args);
    ApprovalRequest request;
    request.tool_name = action.tool_name;
    request.args = action.args;
    request.reason = action.reason.empty() ? decision.reason : action.reason;
    request.risk = decision.effective_risk;
    request.preview_summary = preview.summary;
    request.preview_details = preview.details;

    const bool approved = approval_provider_->approve(request);
    log_debug("Approval", approved ? "Approved by user" : "Denied by user");
    return approved;
}

std::vector<ChatMessage> Agent::build_chat_messages() const {
    std::vector<ChatMessage> raw_messages;

    // System message
    // (build into raw_messages, then compress)
    auto& messages = raw_messages;

    // System message — production-grade agent instructions
    const bool use_compact = runtime_config_.compact_prompt ||
        detect_small_model(client_->model());

    std::ostringstream system_prompt;

    if (use_compact) {
        // Compact system prompt for small models (~200 tokens vs ~800)
        system_prompt << "You are a coding agent. Workspace: " << workspace_root_.string() << "\n";
        system_prompt << "Read code before editing. Verify with build_and_test after changes.\n";
        system_prompt << "Be concise. Use tools when needed.\n";
        system_prompt << "edit_file: path=<file> old<<<text>>> new<<<text>>>\n";
        system_prompt << "write_file: path=<file> content<<<text>>>\n";
        if (runtime_config_.auto_verify) {
            system_prompt << "Auto-verify is on: build_and_test runs after edits. Fix failures immediately.\n";
        }
    } else {
        // Identity and context
        system_prompt << "You are an autonomous software engineering agent. ";
        system_prompt << "You work inside a project workspace and use tools to read, write, search, build, and test code.\n";
        system_prompt << "Workspace: " << workspace_root_.string() << "\n\n";

        // Core principles
        system_prompt << "# Core Principles\n";
        system_prompt << "1. ALWAYS read code before modifying it. Never guess file contents.\n";
        system_prompt << "2. ALWAYS verify changes by running build_and_test after editing code.\n";
        system_prompt << "3. If a build or test fails, read the error, fix the code, and re-run build_and_test. Repeat until passing.\n";
        system_prompt << "4. For complex tasks (3+ steps), use task_planner to create a plan first.\n";
        system_prompt << "5. Make small, incremental changes. Edit one thing, verify, then move on.\n";
        system_prompt << "6. Use git to commit working changes as checkpoints.\n\n";

        // Standard workflow
        system_prompt << "# Standard Workflow\n";
        system_prompt << "For any coding task, follow this sequence:\n";
        system_prompt << "  1. Understand: read_file and search_code to understand existing code\n";
        system_prompt << "  2. Plan: task_planner to decompose if complex\n";
        system_prompt << "  3. Edit: edit_file (preferred) or write_file to make changes\n";
        system_prompt << "  4. Verify: build_and_test to compile and run tests\n";
        system_prompt << "  5. Fix: if verification fails, read errors, fix code, go to step 4\n";
        system_prompt << "  6. Commit: run_command with git add + git commit when tests pass\n";
        system_prompt << "  7. Next: mark step done in task_planner, proceed to next step\n\n";

        // Tool usage guidelines
        system_prompt << "# Tool Guidelines\n";
        system_prompt << "- read_file: Read a file by relative path (e.g., src/main.cpp). Always read before editing.\n";
        system_prompt << "- list_dir: List directory contents. Use to discover project structure.\n";
        system_prompt << "- search_code: Find text in files across the workspace.\n";
        system_prompt << "- edit_file: Modify existing files using exact text replacement.\n";
        system_prompt << "  Format: path=<file> then old<<<exact old text>>> new<<<replacement>>>\n";
        system_prompt << "- write_file: Create new files.\n";
        system_prompt << "  Format: path=<file> then content<<<file content>>>\n";
        system_prompt << "- build_and_test: Compile the project and run tests. Use after every code change.\n";
        system_prompt << "- run_command: Execute shell commands (git, build tools, etc.).\n";
        system_prompt << "- task_planner: Create and track multi-step plans.\n";
        system_prompt << "  Commands: plan:<steps>, done:<step_number>, status\n";
        system_prompt << "- calculator: Evaluate arithmetic expressions.\n\n";

        // Auto-verify
        if (runtime_config_.auto_verify) {
            system_prompt << "# Auto-Verify\n";
            system_prompt << "After you edit code, the system automatically runs build_and_test. ";
            system_prompt << "If it fails, you will see the error output and should fix it immediately.\n\n";
        }

        // Error handling
        system_prompt << "# Error Handling\n";
        system_prompt << "- If a tool returns an error, analyze the error message carefully.\n";
        system_prompt << "- For compilation errors: read the file at the error line, understand the issue, fix it.\n";
        system_prompt << "- For test failures: read the failing test to understand what it expects.\n";
        system_prompt << "- Never skip verification. Never assume code works without testing.\n";
        system_prompt << "- If stuck after 3 attempts, explain the issue and ask the user for guidance.\n\n";

        // Output style
        system_prompt << "# Communication\n";
        system_prompt << "- Be concise. Lead with the action or answer, not the reasoning.\n";
        system_prompt << "- When you complete a task, summarize what you did and the verification result.\n";
        system_prompt << "- If multiple tool calls are needed, make them all in one turn when possible.\n";
    }

    // Load workspace-specific instructions (bolt.md or .bolt/prompt.md)
    const std::string workspace_prompt = load_workspace_prompt(workspace_root_);
    if (!workspace_prompt.empty()) {
        system_prompt << "\n# Project Instructions\n";
        system_prompt << workspace_prompt << "\n";
    }

    // Load and inject auto_load skills
    auto workspace_skills = SkillLoader::discover(workspace_root_ / ".bolt" / "skills");
    std::vector<Skill> global_skills;
    {
        const char* home_env = std::getenv("HOME");
        if (home_env) {
            global_skills = SkillLoader::discover(
                std::filesystem::path(home_env) / ".bolt" / "skills");
        }
    }
    std::string skills_prompt;
    for (const auto& s : workspace_skills) {
        if (s.auto_load) skills_prompt += "\n## Skill: " + s.name + "\n" + s.content + "\n";
    }
    for (const auto& s : global_skills) {
        if (s.auto_load) skills_prompt += "\n## Skill: " + s.name + "\n" + s.content + "\n";
    }
    if (!skills_prompt.empty()) {
        system_prompt << "\n# Active Skills\n" << skills_prompt;
    }

    // Inject remembered context from memory stores
    const std::string global_mem = global_memory_.format_for_prompt();
    const std::string ws_mem = workspace_memory_.format_for_prompt();
    if (!global_mem.empty() || !ws_mem.empty()) {
        system_prompt << "\n# Remembered Context\n";
        if (!global_mem.empty()) system_prompt << global_mem;
        if (!ws_mem.empty()) system_prompt << ws_mem;
        system_prompt << "\n";
    }

    messages.push_back({ChatRole::system, system_prompt.str()});

    // Convert history
    for (const auto& msg : history_) {
        if (msg.role == "user") {
            messages.push_back({ChatRole::user, msg.content});
        } else if (msg.role == "assistant") {
            ChatMessage am;
            am.role = ChatRole::assistant;
            am.content = msg.content;
            am.reasoning_content = msg.reasoning_content;  // For thinking models
            am.tool_calls = msg.tool_calls;  // Restore tool_calls for tool_call_id matching
            messages.push_back(std::move(am));
        } else if (msg.role == "tool") {
            ChatMessage tool_msg;
            tool_msg.role = ChatRole::tool;
            tool_msg.content = msg.content;
            tool_msg.name = msg.name;
            tool_msg.tool_call_id = msg.tool_call_id;
            if (tool_msg.tool_call_id.empty()) {
                // Backward-compatible fallback for older history entries that only stored the tool name.
                for (auto it = messages.rbegin(); it != messages.rend(); ++it) {
                    if (it->role == ChatRole::assistant && !it->tool_calls.empty()) {
                        for (const auto& tc : it->tool_calls) {
                            if (tc.name == msg.name) {
                                tool_msg.tool_call_id = tc.id;
                                break;
                            }
                        }
                        if (!tool_msg.tool_call_id.empty()) break;
                    }
                    // Stop searching once we hit a user message (don't cross conversation turns)
                    if (it->role == ChatRole::user) break;
                }
            }
            messages.push_back(std::move(tool_msg));
        } else if (msg.role == "system") {
            // Don't insert system messages between assistant tool_calls and tool results
            // as this breaks the tool call chain that APIs expect
            bool in_tool_chain = false;
            if (!messages.empty() && messages.back().role == ChatRole::tool) {
                in_tool_chain = true;
            } else if (!messages.empty() && messages.back().role == ChatRole::assistant &&
                       messages.back().has_tool_calls()) {
                in_tool_chain = true;
            }
            if (!in_tool_chain) {
                messages.push_back({ChatRole::user, "[system] " + msg.content});
            }
            // If in tool chain, skip this system message to preserve API format
        }
    }

    // Sanitize: remove orphaned assistant+tool_calls without matching tool results
    // This prevents API errors like "assistant message with tool_calls must be followed by tool messages"
    for (auto it = raw_messages.begin(); it != raw_messages.end(); ) {
        if (it->role == ChatRole::assistant && it->has_tool_calls()) {
            // Check if next message is a tool result
            auto next = it + 1;
            if (next == raw_messages.end() || next->role != ChatRole::tool) {
                // Orphaned: remove the assistant message and strip tool_calls
                it->tool_calls.clear();
                ++it;
            } else {
                ++it;
            }
        } else {
            ++it;
        }
    }

    return prompt_compressor_.compress(raw_messages);
}

std::vector<ToolSchema> Agent::build_tool_schemas() const {
    const bool filter_tools = runtime_config_.core_tools_only ||
        detect_small_model(client_->model());

    static const std::unordered_set<std::string> core_tools = {
        "read_file", "write_file", "edit_file", "list_dir",
        "search_code", "run_command", "build_and_test", "calculator"
    };

    std::vector<ToolSchema> schemas;
    for (const Tool* tool : tools_.list()) {
        if (filter_tools && core_tools.count(tool->name()) == 0) {
            continue;
        }
        schemas.push_back(tool->schema());
    }
    return schemas;
}

std::string Agent::run_turn_structured(const std::string& user_input,
                                       StreamCallback on_token) {
    last_execution_trace_.clear();
    notify_trace_updated();
    auto_verify_count_ = 0;
    speculative_executor_.reset();
    push_history({"user", user_input, ""});

    TaskRunner task_runner(
        policy_, tools_,
        [this](const std::string& title, const std::string& body) { log_debug(title, body); },
        [this](const std::string& stage, const std::string& tool_name, const std::string& target,
               const std::string& detail, bool approved) {
            log_tool_audit(stage, tool_name, target, detail, approved);
        },
        [this](const Action& action, const PolicyDecision& decision, const Tool& tool) {
            return request_approval(action, decision, tool);
        },
        [](const Action& action) { return audit_target_for_action(action); },
        &thread_pool_);

    for (int step = 0; step < runtime_config_.max_model_steps; ++step) {
        if (cancellation_check_ && cancellation_check_()) {
            push_history({"assistant", "[Cancelled by user]", ""});
            return "[Cancelled by user]";
        }

        const std::vector<ChatMessage> messages = build_chat_messages();
        const std::vector<ToolSchema> schemas = build_tool_schemas();

        log_debug("Structured Chat", "messages=" + std::to_string(messages.size()) +
                  " tools=" + std::to_string(schemas.size()));

        // Use streaming if callback provided and client supports it
        ChatMessage response;
        if (on_token && client_->supports_streaming()) {
            std::string accumulated;
            response = client_->chat_streaming(messages, schemas,
                [this, &on_token, &accumulated](const std::string& token) -> bool {
                    accumulated += token;
                    // Speculative prefetch: analyze partial tokens for file paths
                    prefetch_cache_.on_streaming_token(accumulated, workspace_root_);
                    // Speculative execution: detect tool calls early and start running
                    speculative_executor_.on_token(accumulated);
                    // Forward token to caller (SSE passthrough)
                    if (on_token) on_token(token);
                    // Check cancellation
                    if (cancellation_check_ && cancellation_check_()) return false;
                    return true;
                });
        } else {
            response = client_->chat(messages, schemas);
        }
        last_usage_ = response.usage;
        log_debug("Model Response", "content=" + response.content +
                  " tool_calls=" + std::to_string(response.tool_calls.size()));

        // If no tool calls, treat as reply
        if (!response.has_tool_calls()) {
            push_history({"assistant", response.content, ""});
            return response.content;
        }

        // Store assistant message with tool calls in history for context
        Message assistant_msg;
        assistant_msg.role = "assistant";
        assistant_msg.content = response.content;
        assistant_msg.reasoning_content = response.reasoning_content;  // For thinking models
        assistant_msg.tool_calls = response.tool_calls;  // Preserve for tool_call_id matching
        push_history(std::move(assistant_msg));

        // Build actions for ALL tool calls in this response
        std::vector<Action> actions;
        for (const auto& tool_call : response.tool_calls) {
            Action action;
            action.type = ActionType::tool;
            action.tool_name = tool_call.name;
            action.reason = "Model requested via function calling";
            action.risk = "low";
            action.requires_confirmation = false;

            // Parse structured arguments from function calling JSON.
            // Tools that accept JSON directly (edit_file, write_file) get raw JSON.
            // Simple tools (read_file, list_dir) get the first string value.
            try {
                auto j = nlohmann::json::parse(tool_call.arguments);

                // Tools with JSON-native parsers: pass raw JSON directly
                if (action.tool_name == "edit_file" || action.tool_name == "write_file") {
                    action.args = tool_call.arguments;
                } else if (j.contains("args")) {
                    action.args = j["args"].get<std::string>();
                } else if (j.size() == 1 && j.begin()->is_string()) {
                    action.args = j.begin()->get<std::string>();
                } else if (j.contains("command") && j["command"].is_string()) {
                    action.args = j["command"].get<std::string>();
                } else if (j.contains("query") && j["query"].is_string()) {
                    action.args = j["query"].get<std::string>();
                } else if (j.contains("path") && j["path"].is_string() && j.size() == 1) {
                    action.args = j["path"].get<std::string>();
                } else {
                    // Fallback: pass raw JSON, let tool parse it
                    action.args = tool_call.arguments;
                }
            } catch (...) {
                action.args = tool_call.arguments;
            }
            actions.push_back(std::move(action));
        }

        // Mark all steps as planned in trace
        const std::size_t trace_base = last_execution_trace_.size();
        for (std::size_t i = 0; i < actions.size(); ++i) {
            ExecutionStep active_step;
            active_step.index = trace_base + i + 1;
            active_step.tool_name = actions[i].tool_name;
            active_step.args = actions[i].args;
            active_step.reason = actions[i].reason;
            active_step.risk = actions[i].risk;
            active_step.status = ExecutionStepStatus::planned;
            active_step.detail = "Pending execution";
            last_execution_trace_.push_back(active_step);
        }
        notify_trace_updated();

        // Check speculative execution cache for read-only tools.
        // For any tool that was speculatively executed during streaming,
        // substitute the pre-computed result to avoid redundant work.
        std::vector<Action> remaining_actions;
        std::vector<std::size_t> remaining_indices;
        std::vector<TaskStepResult> results(actions.size());
        std::vector<bool> resolved(actions.size(), false);

        for (std::size_t i = 0; i < actions.size(); ++i) {
            const Tool* tool = tools_.find(actions[i].tool_name);
            if (tool && tool->is_read_only()) {
                std::string spec_result =
                    speculative_executor_.wait_result(actions[i].tool_name, actions[i].args, 100);
                if (!spec_result.empty()) {
                    log_debug("Speculative Hit",
                              actions[i].tool_name + " result was pre-computed");
                    TaskStepResult& r = results[i];
                    r.step.index = trace_base + i + 1;
                    r.step.tool_name = actions[i].tool_name;
                    r.step.args = actions[i].args;
                    r.step.reason = actions[i].reason;
                    r.step.risk = actions[i].risk;
                    r.step.status = ExecutionStepStatus::completed;
                    r.step.detail = spec_result;
                    r.observation = {true, "tool", spec_result};
                    resolved[i] = true;
                    continue;
                }
            }
            remaining_actions.push_back(actions[i]);
            remaining_indices.push_back(i);
        }

        // Execute remaining tool calls via execute_batch()
        // (read-only tools run concurrently, write tools run sequentially)
        if (!remaining_actions.empty()) {
            const std::vector<TaskStepResult> batch_results =
                task_runner.execute_batch(remaining_actions, trace_base + 1);
            for (std::size_t j = 0; j < batch_results.size(); ++j) {
                results[remaining_indices[j]] = batch_results[j];
            }
        }

        // Process results and update history
        bool should_return = false;
        std::string early_reply;
        for (std::size_t i = 0; i < results.size(); ++i) {
            last_execution_trace_[trace_base + i] = results[i].step;
            notify_trace_updated();

            std::string history_content;
            std::string history_name = actions[i].tool_name;
            if (results[i].observation.channel == "tool") {
                history_content =
                    std::string(results[i].observation.success ? "TOOL_OK\n" : "TOOL_ERROR\n") +
                    results[i].observation.content;
            } else if (results[i].observation.channel == "policy") {
                history_content = "POLICY_DENY\n" + results[i].observation.content;
                history_name = "policy";
            } else if (results[i].observation.channel == "approval") {
                history_content = "APPROVAL_DENY\n" + results[i].observation.content;
                history_name = "approval";
            } else {
                history_content = results[i].observation.content;
            }

            Message tool_message;
            tool_message.role = "tool";
            tool_message.content = history_content;
            tool_message.name = history_name;
            tool_message.tool_call_id = response.tool_calls[i].id;
            push_history(std::move(tool_message));

            // Track failures for recovery
            if (results[i].step.status == ExecutionStepStatus::failed ||
                results[i].step.status == ExecutionStepStatus::blocked ||
                results[i].step.status == ExecutionStepStatus::denied) {
                failure_tracker_.record_failure(actions[i].tool_name,
                    results[i].observation.content.substr(0, 200));
            } else {
                failure_tracker_.record_success();
            }

            if (results[i].should_return_reply && !should_return) {
                should_return = true;
                early_reply = results[i].reply;
            }
        }

        // Inject recovery guidance if stuck in a failure loop
        if (failure_tracker_.is_stuck()) {
            const std::string diagnostic = failure_tracker_.diagnostic();
            log_debug("Failure Recovery", diagnostic);
            request_router_failure_escalation(client_);
            push_history({"system", diagnostic, ""});
        }

        if (should_return) {
            push_history({"assistant", early_reply, ""});
            return early_reply;
        }

        // Auto-verify: if code was modified, run build_and_test automatically
        if (runtime_config_.auto_verify &&
            auto_verify_count_ < runtime_config_.max_auto_verify_retries) {
            bool code_modified = false;
            for (std::size_t i = 0; i < actions.size(); ++i) {
                const std::string& tname = actions[i].tool_name;
                if ((tname == "edit_file" || tname == "write_file" || tname == "delete_file") &&
                    results[i].observation.success) {
                    code_modified = true;
                    break;
                }
            }

            if (code_modified) {
                const Tool* build_tool = tools_.find("build_and_test");
                if (build_tool) {
                    log_debug("Auto-Verify", "Running build_and_test after code modification (attempt " +
                              std::to_string(auto_verify_count_ + 1) + "/" +
                              std::to_string(runtime_config_.max_auto_verify_retries) + ")");

                    try {
                        ToolResult verify_result = build_tool->run("auto");

                        ExecutionStep verify_step;
                        verify_step.index = last_execution_trace_.size() + 1;
                        verify_step.tool_name = "build_and_test";
                        verify_step.args = "auto";
                        verify_step.reason = "Auto-verify after code edit";
                        verify_step.risk = "low";

                        if (!verify_result.success) {
                            ++auto_verify_count_;
                            verify_step.status = ExecutionStepStatus::failed;
                            verify_step.detail = "Build/test failed: " +
                                verify_result.content.substr(0, 200);
                            last_execution_trace_.push_back(verify_step);
                            notify_trace_updated();

                            // Inject error into history so model sees it and fixes
                            request_router_failure_escalation(client_);
                            std::string error_msg =
                                "AUTO_VERIFY: build_and_test failed after your edit (attempt " +
                                std::to_string(auto_verify_count_) + "/" +
                                std::to_string(runtime_config_.max_auto_verify_retries) +
                                "). Fix the issue.\nError:\n" + verify_result.content;
                            push_history({"system", error_msg, ""});

                            // Let the loop continue so model can fix
                            continue;
                        } else {
                            verify_step.status = ExecutionStepStatus::completed;
                            verify_step.detail = "Build/test passed";
                            last_execution_trace_.push_back(verify_step);
                            notify_trace_updated();

                            push_history({"tool", "TOOL_OK\nAUTO_VERIFY: build_and_test passed.",
                                          "build_and_test", ""});
                            auto_verify_count_ = 0;
                        }
                    } catch (const std::exception& e) {
                        log_debug("Auto-Verify Error", e.what());
                    }
                }
            }
        }
    }

    throw std::runtime_error("Agent exceeded the maximum number of model steps");
}

void Agent::log_debug(const std::string& title, const std::string& body) const {
    if (!debug_) {
        return;
    }

    std::cerr << "\n[debug] " << title << "\n";
    std::cerr << body << "\n";
}
