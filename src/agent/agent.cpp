#include "agent.h"

#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <utility>

#include <nlohmann/json.hpp>

#include "action_parser.h"

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
    return message.role.size() + message.name.size() + message.content.size() + 32;
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
      debug_(debug) {
    if (client_ == nullptr) {
        throw std::invalid_argument("Agent requires a model client");
    }
    if (approval_provider_ == nullptr) {
        throw std::invalid_argument("Agent requires an approval provider");
    }
    if (tools_.empty()) {
        throw std::invalid_argument("Agent requires a tool registry");
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
        [](const Action& action) { return audit_target_for_action(action); });

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

void Agent::push_history(Message message) {
    history_.push_back(std::move(message));
    enforce_history_budget();
}

void Agent::enforce_history_budget() {
    while (history_.size() > runtime_config_.history_window) {
        history_.erase(history_.begin());
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
    std::ostringstream prompt;
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
    std::vector<ChatMessage> messages;

    // System message
    std::ostringstream system_prompt;
    system_prompt << "You are a local C++ coding agent running inside a workspace.\n";
    system_prompt << "Workspace root: " << workspace_root_.string() << "\n\n";
    system_prompt << "Decision rules:\n";
    system_prompt << "- Use the appropriate tool when the user asks about files, code, calculations, or system state.\n";
    system_prompt << "- write_file, edit_file, and run_command require user approval.\n";
    system_prompt << "- If a tool already gives the needed facts, reply directly.\n";
    system_prompt << "- Never invent file contents.\n";
    messages.push_back({ChatRole::system, system_prompt.str()});

    // Convert history
    for (const auto& msg : history_) {
        if (msg.role == "user") {
            messages.push_back({ChatRole::user, msg.content});
        } else if (msg.role == "assistant") {
            messages.push_back({ChatRole::assistant, msg.content});
        } else if (msg.role == "tool") {
            ChatMessage tool_msg;
            tool_msg.role = ChatRole::tool;
            tool_msg.content = msg.content;
            tool_msg.name = msg.name;
            // Find the tool_call_id from the previous assistant message
            if (!messages.empty()) {
                const auto& prev = messages.back();
                if (prev.role == ChatRole::assistant) {
                    for (const auto& tc : prev.tool_calls) {
                        if (tc.name == msg.name) {
                            tool_msg.tool_call_id = tc.id;
                            break;
                        }
                    }
                }
            }
            messages.push_back(std::move(tool_msg));
        } else if (msg.role == "system") {
            messages.push_back({ChatRole::user, "[system] " + msg.content});
        }
    }

    return messages;
}

std::vector<ToolSchema> Agent::build_tool_schemas() const {
    std::vector<ToolSchema> schemas;
    for (const Tool* tool : tools_.list()) {
        schemas.push_back(tool->schema());
    }
    return schemas;
}

std::string Agent::run_turn_structured(const std::string& user_input) {
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
        [](const Action& action) { return audit_target_for_action(action); });

    for (int step = 0; step < runtime_config_.max_model_steps; ++step) {
        const std::vector<ChatMessage> messages = build_chat_messages();
        const std::vector<ToolSchema> schemas = build_tool_schemas();

        log_debug("Structured Chat", "messages=" + std::to_string(messages.size()) +
                  " tools=" + std::to_string(schemas.size()));

        const ChatMessage response = client_->chat(messages, schemas);
        log_debug("Model Response", "content=" + response.content +
                  " tool_calls=" + std::to_string(response.tool_calls.size()));

        // If no tool calls, treat as reply
        if (!response.has_tool_calls()) {
            push_history({"assistant", response.content, ""});
            return response.content;
        }

        // Store assistant message with tool calls in history for context
        // (Simplified: store the text content as the assistant turn)
        Message assistant_msg;
        assistant_msg.role = "assistant";
        assistant_msg.content = response.content;
        // We need to track tool_calls for the next message's tool_call_id
        // Store as a special history entry
        push_history(std::move(assistant_msg));

        // Execute each tool call
        for (const auto& tool_call : response.tool_calls) {
            Action action;
            action.type = ActionType::tool;
            action.tool_name = tool_call.name;
            action.reason = "Model requested via function calling";
            action.risk = "low";
            action.requires_confirmation = false;

            // Parse structured arguments back to the string format tools expect
            try {
                auto j = nlohmann::json::parse(tool_call.arguments);
                if (j.contains("args")) {
                    action.args = j["args"].get<std::string>();
                } else {
                    // For tools that use structured params, serialize as key=value lines
                    std::ostringstream args_str;
                    for (auto it = j.begin(); it != j.end(); ++it) {
                        if (it->is_string()) {
                            args_str << it.key() << "=" << it->get<std::string>() << "\n";
                        } else {
                            args_str << it.key() << "=" << it->dump() << "\n";
                        }
                    }
                    action.args = args_str.str();
                    // Trim trailing newline
                    if (!action.args.empty() && action.args.back() == '\n') {
                        action.args.pop_back();
                    }
                }
            } catch (...) {
                action.args = tool_call.arguments;
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
