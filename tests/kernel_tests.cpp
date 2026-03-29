#include <functional>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "../src/agent/action_parser.h"
#include "../src/agent/agent.h"
#include "../src/agent/calculator_tool.h"
#include "../src/agent/click_element_tool.h"
#include "../src/agent/edit_file_tool.h"
#include "../src/agent/focus_window_tool.h"
#include "../src/agent/inspect_ui_tool.h"
#include "../src/agent/list_processes_tool.h"
#include "../src/agent/list_windows_tool.h"
#include "../src/agent/open_app_tool.h"
#include "../src/agent/permission_policy.h"
#include "../src/agent/run_command_tool.h"
#include "../src/agent/type_text_tool.h"
#include "../src/agent/tool_set_factory.h"
#include "../src/agent/wait_for_window_tool.h"
#include "../src/agent/write_file_tool.h"
#include "../src/app/app_config.h"
#include "../src/app/agent_factory.h"
#include "../src/app/agent_cli_options.h"
#include "../src/app/agent_services.h"
#include "../src/app/agent_runner.h"
#include "../src/app/program_cli.h"
#include "../src/app/self_check_runner.h"
#include "../src/app/static_approval_provider.h"
#include "../src/app/web_chat_cli_options.h"
#include "../src/core/interfaces/audit_logger.h"
#include "../src/platform/platform_agent_factory.h"
#ifdef _WIN32
#include "../src/platform/windows/ollama_json_utils.h"
#include "../src/platform/windows/windows_agent_factory.h"
#endif

namespace {

class ScopedTempDir {
public:
    ScopedTempDir() {
        const auto base = std::filesystem::temp_directory_path();
        path_ = base / std::filesystem::path("bolt_tests_" +
                                             std::to_string(
                                                 static_cast<unsigned long long>(
                                                     std::filesystem::file_time_type::clock::now()
                                                         .time_since_epoch()
                                                         .count())));
        std::filesystem::create_directories(path_);
    }

    ~ScopedTempDir() {
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
    }

    const std::filesystem::path& path() const {
        return path_;
    }

private:
    std::filesystem::path path_;
};

class LocalTestFileSystem : public IFileSystem {
public:
    bool exists(const std::filesystem::path& path) const override {
        return std::filesystem::exists(path);
    }

    bool is_directory(const std::filesystem::path& path) const override {
        return std::filesystem::is_directory(path);
    }

    bool is_regular_file(const std::filesystem::path& path) const override {
        return std::filesystem::is_regular_file(path);
    }

    DirectoryListResult list_directory(const std::filesystem::path&) const override {
        return {false, {}, "Not implemented in tests"};
    }

    bool create_directories(const std::filesystem::path& path,
                            std::string& error) const override {
        std::error_code ec;
        const bool created = std::filesystem::create_directories(path, ec);
        if (ec) {
            error = ec.message();
            return false;
        }
        (void)created;
        return true;
    }

    FileReadResult read_text_file(const std::filesystem::path& path) const override {
        std::ifstream input(path, std::ios::binary);
        if (!input) {
            return {false, "", "Failed to open file for reading"};
        }

        std::string content((std::istreambuf_iterator<char>(input)),
                            std::istreambuf_iterator<char>());
        return {true, content, ""};
    }

    FileWriteResult write_text_file(const std::filesystem::path& path,
                                    const std::string& content) const override {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        if (!output) {
            return {false, "Failed to open file for writing"};
        }

        output << content;
        if (!output.good()) {
            return {false, "Failed to write file"};
        }
        return {true, ""};
    }

    TextSearchResult search_text(const std::filesystem::path&,
                                 const std::string&,
                                 std::size_t,
                                 std::uintmax_t) const override {
        return {false, {}, false, "Not implemented in tests"};
    }
};

class RecordingCommandRunner : public ICommandRunner {
public:
    mutable std::string last_command;
    mutable std::filesystem::path last_working_directory;
    mutable std::size_t last_timeout_ms = 0;
    CommandExecutionResult result{true, false, 0, "", ""};

    CommandExecutionResult run(const std::string& command,
                               const std::filesystem::path& working_directory,
                               std::size_t timeout_ms) const override {
        last_command = command;
        last_working_directory = working_directory;
        last_timeout_ms = timeout_ms;
        return result;
    }
};

class RecordingProcessManager : public IProcessManager {
public:
    mutable std::string last_command_line;
    ProcessListResult list_result{true, {}, ""};
    LaunchProcessResult launch_result{true, 4321, ""};

    ProcessListResult list_processes() const override {
        return list_result;
    }

    LaunchProcessResult launch_process(const std::string& command_line) const override {
        last_command_line = command_line;
        return launch_result;
    }
};

class RecordingWindowController : public IWindowController {
public:
    mutable WindowFocusTarget last_target;
    WindowListResult list_result{true, {}, ""};
    WindowFocusResult focus_result{true, {}, ""};

    WindowListResult list_windows() const override {
        return list_result;
    }

    WindowFocusResult focus_window(const WindowFocusTarget& target) const override {
        last_target = target;
        return focus_result;
    }
};

class RecordingUiAutomation : public IUiAutomation {
public:
    mutable std::string last_text;
    mutable ClickElementTarget last_click_target;
    mutable InspectUiRequest last_inspect_request;
    InspectUiResult inspect_result{true, "", "", {}, ""};
    ClickElementResult click_result{true, {}, ""};
    TypeTextResult result{true, 0, ""};

    InspectUiResult inspect_ui(const InspectUiRequest& request) const override {
        last_inspect_request = request;
        return inspect_result;
    }

    ClickElementResult click_element(const ClickElementTarget& target) const override {
        last_click_target = target;
        return click_result;
    }

    TypeTextResult type_text(const std::string& text) const override {
        last_text = text;
        if (result.success && result.characters_sent == 0) {
            return {true, text.size(), ""};
        }
        return result;
    }
};

class ScriptedModelClient : public IModelClient {
public:
    explicit ScriptedModelClient(std::vector<std::string> responses,
                                 std::string model_name = "test-model")
        : responses_(std::move(responses)),
          model_name_(std::move(model_name)) {}

    std::string generate(const std::string& prompt) const override {
        prompts_.push_back(prompt);
        if (next_response_ >= responses_.size()) {
            throw std::runtime_error("Scripted model client ran out of responses");
        }
        return responses_[next_response_++];
    }

    const std::string& model() const override {
        return model_name_;
    }

    const std::vector<std::string>& prompts() const {
        return prompts_;
    }

private:
    std::vector<std::string> responses_;
    std::string model_name_;
    mutable std::vector<std::string> prompts_;
    mutable std::size_t next_response_ = 0;
};

class RecordingApprovalProvider : public IApprovalProvider {
public:
    explicit RecordingApprovalProvider(std::vector<bool> decisions)
        : decisions_(std::move(decisions)) {}

    bool approve(const ApprovalRequest& request) override {
        requests.push_back(request);
        if (next_decision_ >= decisions_.size()) {
            return false;
        }
        return decisions_[next_decision_++];
    }

    std::vector<ApprovalRequest> requests;

private:
    std::vector<bool> decisions_;
    std::size_t next_decision_ = 0;
};

class RecordingAuditLogger : public IAuditLogger {
public:
    void log(const AuditEvent& event) override {
        events.push_back(event);
    }

    std::vector<AuditEvent> events;
};

ToolRegistry make_test_tool_registry(
    const std::filesystem::path& workspace_root,
    const std::shared_ptr<IFileSystem>& file_system,
    const std::shared_ptr<ICommandRunner>& command_runner,
    const std::shared_ptr<IAuditLogger>& audit_logger = nullptr,
    const std::shared_ptr<IProcessManager>& process_manager = nullptr,
    const std::shared_ptr<IUiAutomation>& ui_automation = nullptr,
    const std::shared_ptr<IWindowController>& window_controller = nullptr) {
    return create_default_tool_registry(workspace_root, file_system, command_runner, audit_logger,
                                        {}, process_manager, ui_automation, window_controller);
}

void expect_true(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void expect_equal(const std::string& actual, const std::string& expected,
                  const std::string& message) {
    if (actual != expected) {
        throw std::runtime_error(message + " (expected '" + expected + "', got '" + actual + "')");
    }
}

void expect_equal(bool actual, bool expected, const std::string& message) {
    if (actual != expected) {
        throw std::runtime_error(message + " (expected " + (expected ? "true" : "false") +
                                 ", got " + (actual ? "true" : "false") + ")");
    }
}

void write_text_file(const std::filesystem::path& path, const std::string& content) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        throw std::runtime_error("Failed to prepare test file: " + path.string());
    }
    output << content;
}

std::string read_text_file(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("Failed to read test file: " + path.string());
    }
    return std::string((std::istreambuf_iterator<char>(input)),
                       std::istreambuf_iterator<char>());
}

const CapabilityState& find_capability_state(const std::vector<CapabilityState>& states,
                                             const std::string& name) {
    for (const CapabilityState& state : states) {
        if (state.name == name) {
            return state;
        }
    }
    throw std::runtime_error("Capability state not found: " + name);
}

void expect_action_reply_parses_with_metadata() {
    const Action action = parse_action_response(
        R"(prefix {"action":"reply","content":"done","reason":"safe","risk":"LOW","requires_confirmation":"yes"} suffix)");

    expect_true(action.type == ActionType::reply, "Reply action type should parse");
    expect_equal(action.content, "done", "Reply content should parse");
    expect_equal(action.reason, "safe", "Reply reason should parse");
    expect_equal(action.risk, "low", "Reply risk should normalize to lowercase");
    expect_equal(action.requires_confirmation, true,
                 "Reply requires_confirmation should parse truthy strings");
}

void expect_action_tool_parses_escaped_args() {
    const Action action = parse_action_response(
        R"({"action":"tool","tool":"read_file","args":"line1\n\"quoted\"","reason":"inspect","risk":"medium","requires_confirmation":"false"})");

    expect_true(action.type == ActionType::tool, "Tool action type should parse");
    expect_equal(action.tool_name, "read_file", "Tool name should parse");
    expect_equal(action.args, "line1\n\"quoted\"", "Tool args should unescape JSON text");
    expect_equal(action.reason, "inspect", "Tool reason should parse");
    expect_equal(action.risk, "medium", "Tool risk should parse");
    expect_equal(action.requires_confirmation, false,
                 "Tool requires_confirmation should parse false");
}

void expect_action_parser_decodes_unicode_escape_sequences() {
    const Action action = parse_action_response(
        R"({"action":"reply","content":"\u4F60\u597D \uD83D\uDE80","reason":"unicode","risk":"low","requires_confirmation":"false"})");

    const std::string expected = "\xE4\xBD\xA0\xE5\xA5\xBD \xF0\x9F\x9A\x80";
    expect_equal(action.content, expected,
                 "Parser should decode unicode escapes into UTF-8 content");
}

void expect_action_parser_rejects_missing_action() {
    bool threw = false;
    try {
        (void)parse_action_response(R"({"tool":"read_file","args":"src/main.cpp"})");
    } catch (const std::runtime_error&) {
        threw = true;
    }
    expect_true(threw, "Parser should reject responses without action");
}

void expect_policy_allows_read_without_approval() {
    const Action action{ActionType::tool, "read_file", "", "src/main.cpp", "", "", false};
    const PolicyDecision decision = PermissionPolicy().evaluate(action);

    expect_equal(decision.allowed, true, "Read-only tool should be allowed");
    expect_equal(decision.approval_required, false,
                 "Read-only tool should not require approval by default");
    expect_equal(decision.effective_risk, "low", "Read-only tool should default to low risk");
}

void expect_policy_allows_desktop_read_without_approval() {
    const Action action{ActionType::tool, "list_processes", "", "", "", "", false};
    const PolicyDecision decision = PermissionPolicy().evaluate(action);

    expect_equal(decision.allowed, true, "Desktop inspection tool should be allowed");
    expect_equal(decision.approval_required, false,
                 "Desktop inspection tool should not require approval by default");
    expect_equal(decision.effective_risk, "low",
                 "Desktop inspection tool should default to low risk");
}

void expect_policy_ignores_confirmation_flag_for_read_only_tool() {
    const Action action{ActionType::tool, "list_processes", "", "", "", "low", true};
    const PolicyDecision decision = PermissionPolicy().evaluate(action);

    expect_equal(decision.allowed, true,
                 "Read-only tools should stay allowed even if the model requests confirmation");
    expect_equal(decision.approval_required, false,
                 "Read-only tools should not require approval because of model output");
}

void expect_policy_requires_approval_for_write() {
    const Action action{ActionType::tool, "edit_file", "", "path=src/main.cpp", "", "", false};
    const PolicyDecision decision = PermissionPolicy().evaluate(action);

    expect_equal(decision.allowed, false, "Write tool should not auto-run");
    expect_equal(decision.approval_required, true, "Write tool should require approval");
    expect_equal(decision.effective_risk, "medium",
                 "Write tool should be elevated to medium risk");
}

void expect_policy_requires_approval_for_desktop_control() {
    const Action action{ActionType::tool, "open_app", "", "notepad.exe", "", "", false};
    const PolicyDecision decision = PermissionPolicy().evaluate(action);

    expect_equal(decision.allowed, false, "Desktop control should not auto-run");
    expect_equal(decision.approval_required, true,
                 "Desktop control should require approval");
    expect_equal(decision.effective_risk, "medium",
                 "Desktop control should default to medium risk");
}

void expect_policy_blocks_high_risk_action() {
    const Action action{ActionType::tool, "run_command", "", "command=git clean -fd", "",
                        "high", false};
    const PolicyDecision decision = PermissionPolicy().evaluate(action);

    expect_equal(decision.allowed, false, "High-risk action should be blocked");
    expect_equal(decision.approval_required, false,
                 "High-risk action should be blocked, not approved");
    expect_equal(decision.effective_risk, "high", "High-risk action should stay high");
}

void expect_policy_config_can_disable_high_risk_block() {
    PolicyConfig config;
    config.block_high_risk = false;

    const Action action{ActionType::tool, "run_command", "", "ollama list", "",
                        "high", false};
    const PolicyDecision decision = PermissionPolicy(config).evaluate(action);

    expect_equal(decision.allowed, false, "High-risk command should still not auto-run");
    expect_equal(decision.approval_required, true,
                 "High-risk command should fall back to approval when high-risk blocking is off");
    expect_equal(decision.effective_risk, "high",
                 "Effective risk should stay high when approval is required");
}

void expect_policy_config_can_reclassify_tool_bucket() {
    PolicyConfig config;
    config.read_only_tools.erase("search_code");
    config.bounded_command_tools.insert("search_code");

    const Action action{ActionType::tool, "search_code", "", "query=ToolRegistry", "", "", false};
    const PolicyDecision decision = PermissionPolicy(config).evaluate(action);

    expect_equal(decision.allowed, false, "Reclassified tool should no longer auto-run");
    expect_equal(decision.approval_required, true,
                 "Reclassified tool should follow its configured approval bucket");
    expect_equal(decision.effective_risk, "medium",
                 "Reclassified command-like tool should be treated as medium risk");
}

#ifdef _WIN32
void expect_ollama_json_escape_preserves_control_chars() {
    const std::string escaped =
        ollama_json::escape_json_string("say \"hi\"\nnext\tline\\done");
    expect_equal(escaped, "say \\\"hi\\\"\\nnext\\tline\\\\done",
                 "JSON escaping should cover quotes, newlines, tabs, and backslashes");
}

void expect_ollama_json_extracts_top_level_string() {
    const std::string value = ollama_json::extract_top_level_json_string_field(
        R"({"response":"hello","error":"","details":{"response":"nested"}})", "response");
    expect_equal(value, "hello", "Top-level response field should be extracted");
}

void expect_ollama_json_handles_escaped_strings() {
    const std::string value = ollama_json::extract_top_level_json_string_field(
        R"({"response":"line1\n\"quoted\"","done":true})", "response");
    expect_equal(value, "line1\n\"quoted\"", "JSON extraction should unescape content");
}

void expect_ollama_json_returns_empty_for_non_string_field() {
    const std::string value = ollama_json::extract_top_level_json_string_field(
        R"({"response":{"text":"nested"}})", "response");
    expect_equal(value, "", "Non-string fields should return empty");
}

void expect_ollama_json_rejects_invalid_json() {
    bool threw = false;
    try {
        (void)ollama_json::extract_top_level_json_string_field("[]", "response");
    } catch (const std::runtime_error&) {
        threw = true;
    }
    expect_true(threw, "Invalid top-level JSON should throw");
}
#endif  // _WIN32

void expect_app_config_reads_workspace_file() {
    ScopedTempDir temp_dir;
    write_text_file(temp_dir.path() / "bolt.conf",
                    "default_model=phi4-mini\n"
                    "ollama.host=localhost\n"
                    "ollama.port=12345\n"
                    "ollama.path=/api/custom-generate\n"
                    "ollama.receive_timeout_ms=123456\n"
                    "commands.allowed_executables=git,ollama\n"
                    "commands.timeout_ms=4321\n"
                    "commands.max_output_bytes=512\n"
                    "commands.allowed_subcommands.git=status,show\n"
                    "commands.allowed_subcommands.ollama=list\n"
                    "policy.read_only_tools=read_file,list_dir\n"
                    "policy.bounded_write_tools=write_file\n"
                    "policy.bounded_command_tools=run_command,search_code\n"
                    "policy.bounded_desktop_tools=open_app,focus_window\n"
                    "policy.block_high_risk=false\n"
                    "agent.default_debug=true\n"
                    "agent.max_model_steps=7\n"
                    "agent.history_window=4\n"
                    "agent.history_byte_budget=2048\n"
                    "approval.mode=auto-approve\n");

    const AppConfig config = load_app_config(temp_dir.path());
    expect_equal(config.default_model, "phi4-mini", "Config file should override default model");
    expect_equal(config.ollama.host, "localhost", "Config file should override Ollama host");
    expect_true(config.ollama.port == 12345, "Config file should override Ollama port");
    expect_equal(config.ollama.generate_path, "/api/custom-generate",
                 "Config file should override Ollama path");
    expect_true(config.ollama.receive_timeout_ms == 123456,
                "Config file should override Ollama receive timeout");
    expect_true(config.command_policy.allowed_executables.count("git") == 1,
                "Config file should keep configured executables");
    expect_true(config.command_policy.allowed_executables.count("cmake") == 0,
                "Config file should replace default executable whitelist");
    expect_true(config.command_policy.timeout_ms == 4321,
                "Config file should override command timeout");
    expect_true(config.command_policy.max_output_bytes == 512,
                "Config file should override command output truncation");
    expect_true(config.command_policy.allowed_subcommands.at("git").count("show") == 1,
                "Config file should override git subcommands");
    expect_true(config.command_policy.allowed_subcommands.at("git").count("diff") == 0,
                "Config file should replace git subcommands");
    expect_true(config.policy.read_only_tools.count("read_file") == 1,
                "Config file should override read-only policy tools");
    expect_true(config.policy.read_only_tools.count("calculator") == 0,
                "Config file should replace default read-only tool bucket");
    expect_true(config.policy.bounded_command_tools.count("search_code") == 1,
                "Config file should override bounded command policy tools");
    expect_true(config.policy.bounded_desktop_tools.count("open_app") == 1,
                "Config file should override bounded desktop policy tools");
    expect_equal(config.policy.block_high_risk, false,
                 "Config file should override policy.block_high_risk");
    expect_equal(config.agent_runtime.default_debug, true,
                 "Config file should override agent.default_debug");
    expect_true(config.agent_runtime.max_model_steps == 7,
                "Config file should override agent.max_model_steps");
    expect_true(config.agent_runtime.history_window == 4,
                "Config file should override agent.history_window");
    expect_true(config.agent_runtime.history_byte_budget == 2048,
                "Config file should override agent.history_byte_budget");
    expect_true(config.approval.mode == ApprovalMode::auto_approve,
                "Config file should override approval.mode");
}

void expect_edit_file_exact_replace_updates_file() {
    ScopedTempDir temp_dir;
    const auto file_system = std::make_shared<LocalTestFileSystem>();
    const std::filesystem::path file_path = temp_dir.path() / "notes.txt";
    write_text_file(file_path, "alpha\nbeta\ngamma\n");

    EditFileTool tool(temp_dir.path(), file_system);
    const ToolPreview preview = tool.preview("path=notes.txt\nold<<<\nbeta\n>>>\nnew<<<\ndelta\n>>>");
    expect_true(preview.summary.find("notes.txt") != std::string::npos,
                "Edit preview should include the relative path");

    const ToolResult result = tool.run("path=notes.txt\nold<<<\nbeta\n>>>\nnew<<<\ndelta\n>>>");
    expect_equal(result.success, true, "Exact replace should succeed");
    expect_equal(read_text_file(file_path), "alpha\ndelta\ngamma\n",
                 "Exact replace should update file contents");
}

void expect_edit_file_line_patch_updates_file() {
    ScopedTempDir temp_dir;
    const auto file_system = std::make_shared<LocalTestFileSystem>();
    const std::filesystem::path file_path = temp_dir.path() / "notes.txt";
    write_text_file(file_path, "one\ntwo\nthree\nfour\n");

    EditFileTool tool(temp_dir.path(), file_system);
    const ToolResult result =
        tool.run("path=notes.txt\nreplace_lines=2-3\ncontent<<<\nTWO\nTHREE\n>>>");
    expect_equal(result.success, true, "Line patch should succeed");
    expect_equal(read_text_file(file_path), "one\nTWO\nTHREE\nfour\n",
                 "Line patch should replace the targeted line range");
}

void expect_write_file_logs_execution_audit() {
    ScopedTempDir temp_dir;
    const auto file_system = std::make_shared<LocalTestFileSystem>();
    const auto audit_logger = std::make_shared<RecordingAuditLogger>();

    WriteFileTool tool(temp_dir.path(), file_system, audit_logger);
    const ToolResult result = tool.run("path=notes.txt\ncontent<<<\nhello audit\n>>>");

    expect_equal(result.success, true, "write_file should still succeed with audit logging");
    expect_true(audit_logger->events.size() == 1,
                "write_file should emit one audit event on success");
    expect_equal(audit_logger->events[0].stage, "executed",
                 "write_file audit should record execution");
    expect_equal(audit_logger->events[0].tool_name, "write_file",
                 "write_file audit should record the tool name");
    expect_equal(audit_logger->events[0].target, "notes.txt",
                 "write_file audit should record the workspace-relative path");
}

void expect_edit_file_logs_execution_audit() {
    ScopedTempDir temp_dir;
    const auto file_system = std::make_shared<LocalTestFileSystem>();
    const auto audit_logger = std::make_shared<RecordingAuditLogger>();
    const std::filesystem::path file_path = temp_dir.path() / "notes.txt";
    write_text_file(file_path, "alpha\nbeta\n");

    EditFileTool tool(temp_dir.path(), file_system, audit_logger);
    const ToolResult result =
        tool.run("path=notes.txt\nold<<<\nbeta\n>>>\nnew<<<\ndelta\n>>>");

    expect_equal(result.success, true, "edit_file should still succeed with audit logging");
    expect_true(audit_logger->events.size() == 1,
                "edit_file should emit one audit event on success");
    expect_equal(audit_logger->events[0].stage, "executed",
                 "edit_file audit should record execution");
    expect_equal(audit_logger->events[0].tool_name, "edit_file",
                 "edit_file audit should record the tool name");
    expect_equal(audit_logger->events[0].target, "notes.txt",
                 "edit_file audit should record the workspace-relative path");
}

void expect_edit_file_rejects_ambiguous_match() {
    ScopedTempDir temp_dir;
    const auto file_system = std::make_shared<LocalTestFileSystem>();
    const std::filesystem::path file_path = temp_dir.path() / "notes.txt";
    write_text_file(file_path, "beta\nbeta\n");

    EditFileTool tool(temp_dir.path(), file_system);
    const ToolResult result = tool.run("path=notes.txt\nold<<<\nbeta\n>>>\nnew<<<\ndelta\n>>>");
    expect_equal(result.success, false, "Ambiguous exact replace should fail");
    expect_true(result.content.find("matched multiple locations") != std::string::npos,
                "Ambiguous exact replace should explain the failure");
}

void expect_list_processes_formats_processes() {
    auto process_manager = std::make_shared<RecordingProcessManager>();
    process_manager->list_result.processes = {
        {1024, "code.exe"},
        {2048, "notepad.exe"},
    };

    ListProcessesTool tool(process_manager);
    const ToolResult result = tool.run("note");

    expect_equal(result.success, true, "list_processes should succeed");
    expect_true(result.content.find("MATCHES: 1") != std::string::npos,
                "list_processes should report the filtered match count");
    expect_true(result.content.find("2048 notepad.exe") != std::string::npos,
                "list_processes should render matching processes");
}

void expect_list_windows_formats_windows() {
    auto window_controller = std::make_shared<RecordingWindowController>();
    window_controller->list_result.windows = {
        {"0x101", 777, "README - Notepad", "Notepad", true},
        {"0x102", 888, "Terminal", "ConsoleWindowClass", true},
    };

    ListWindowsTool tool(window_controller);
    const ToolResult result = tool.run("readme");

    expect_equal(result.success, true, "list_windows should succeed");
    expect_true(result.content.find("MATCHES: 1") != std::string::npos,
                "list_windows should report the filtered match count");
    expect_true(result.content.find("0x101 pid=777 class=Notepad title=README - Notepad") !=
                    std::string::npos,
                "list_windows should render matching windows");
}

void expect_open_app_logs_execution_audit() {
    auto process_manager = std::make_shared<RecordingProcessManager>();
    auto audit_logger = std::make_shared<RecordingAuditLogger>();

    OpenAppTool tool(process_manager, audit_logger);
    const ToolResult result = tool.run("notepad.exe");

    expect_equal(result.success, true, "open_app should succeed for a valid command line");
    expect_equal(process_manager->last_command_line, "notepad.exe",
                 "open_app should pass the command line to the process manager");
    expect_true(audit_logger->events.size() == 1,
                "open_app should emit one audit event on success");
    expect_equal(audit_logger->events[0].stage, "executed",
                 "open_app audit should record execution");
    expect_equal(audit_logger->events[0].tool_name, "open_app",
                 "open_app audit should record the tool name");
}

void expect_focus_window_logs_execution_audit() {
    auto window_controller = std::make_shared<RecordingWindowController>();
    auto audit_logger = std::make_shared<RecordingAuditLogger>();
    window_controller->focus_result = {true,
                                       {"0xABC", 4321, "README - Notepad", "Notepad", true},
                                       ""};

    FocusWindowTool tool(window_controller, audit_logger);
    const ToolResult result = tool.run("title=README - Notepad");

    expect_equal(result.success, true, "focus_window should succeed for a known title");
    expect_equal(window_controller->last_target.title, "README - Notepad",
                 "focus_window should parse the window title");
    expect_true(audit_logger->events.size() == 1,
                "focus_window should emit one audit event on success");
    expect_equal(audit_logger->events[0].stage, "executed",
                 "focus_window audit should record execution");
    expect_equal(audit_logger->events[0].tool_name, "focus_window",
                 "focus_window audit should record the tool name");
}

void expect_wait_for_window_finds_matching_window() {
    auto window_controller = std::make_shared<RecordingWindowController>();
    window_controller->list_result.windows = {
        {"0x500", 2222, "Untitled - Notepad", "Notepad", true},
    };

    WaitForWindowTool tool(window_controller);
    const ToolResult result = tool.run("title=Notepad\ntimeout_ms=50");

    expect_equal(result.success, true, "wait_for_window should succeed when a matching window exists");
    expect_true(result.content.find("FOUND WINDOW") != std::string::npos,
                "wait_for_window should report the found window");
    expect_true(result.content.find("Untitled - Notepad") != std::string::npos,
                "wait_for_window should include the matching title");
}

void expect_type_text_logs_execution_audit() {
    auto ui_automation = std::make_shared<RecordingUiAutomation>();
    auto audit_logger = std::make_shared<RecordingAuditLogger>();

    TypeTextTool tool(ui_automation, audit_logger);
    const ToolResult result = tool.run("text<<<\nhello world\n>>>");

    expect_equal(result.success, true, "type_text should succeed with a valid text payload");
    expect_equal(ui_automation->last_text, "hello world",
                 "type_text should pass the parsed text to UI automation");
    expect_true(audit_logger->events.size() == 1,
                "type_text should emit one audit event on success");
    expect_equal(audit_logger->events[0].stage, "executed",
                 "type_text audit should record execution");
    expect_equal(audit_logger->events[0].tool_name, "type_text",
                 "type_text audit should record the tool name");
}

void expect_inspect_ui_formats_elements() {
    auto ui_automation = std::make_shared<RecordingUiAutomation>();
    ui_automation->inspect_result = {true,
                                     "0x111",
                                     "Untitled - Notepad",
                                     {
                                         {"0x111", "", "Notepad", "Untitled - Notepad", true, true},
                                         {"0x222", "0x111", "Edit", "", true, true},
                                     },
                                     ""};

    InspectUiTool tool(ui_automation);
    const ToolResult result = tool.run("max_elements=10");

    expect_equal(result.success, true, "inspect_ui should succeed with a valid inspection result");
    expect_true(ui_automation->last_inspect_request.max_elements == 10,
                "inspect_ui should forward max_elements to the automation layer");
    expect_true(result.content.find("WINDOW_TITLE: Untitled - Notepad") != std::string::npos,
                "inspect_ui should render the root window title");
    expect_true(result.content.find("0x222 parent=0x111 class=Edit") != std::string::npos,
                "inspect_ui should render child element rows");
}

void expect_click_element_logs_execution_audit() {
    auto ui_automation = std::make_shared<RecordingUiAutomation>();
    auto audit_logger = std::make_shared<RecordingAuditLogger>();
    ui_automation->click_result = {true,
                                   {"0x444", "0x111", "Button", "Save", true, true},
                                   ""};

    ClickElementTool tool(ui_automation, audit_logger);
    const ToolResult result = tool.run("text=Save\nclass=Button");

    expect_equal(result.success, true, "click_element should succeed for a matching selector");
    expect_equal(ui_automation->last_click_target.text, "Save",
                 "click_element should parse the text selector");
    expect_equal(ui_automation->last_click_target.class_name, "Button",
                 "click_element should parse the class selector");
    expect_true(audit_logger->events.size() == 1,
                "click_element should emit one audit event on success");
    expect_equal(audit_logger->events[0].stage, "executed",
                 "click_element audit should record execution");
    expect_equal(audit_logger->events[0].tool_name, "click_element",
                 "click_element audit should record the tool name");
}

void expect_run_command_executes_allowed_command() {
    ScopedTempDir temp_dir;
    auto runner = std::make_shared<RecordingCommandRunner>();
    runner->result = {true, false, 0, "clean", ""};

    RunCommandTool tool(temp_dir.path(), runner);
    const ToolResult result = tool.run("git status");

    expect_equal(result.success, true, "Whitelisted command should succeed");
    expect_equal(runner->last_command, "git status",
                 "Command runner should receive the validated command");
    expect_equal(runner->last_working_directory.string(), temp_dir.path().string(),
                 "Command runner should receive the workspace root");
    expect_true(runner->last_timeout_ms == CommandPolicyConfig{}.timeout_ms,
                "Command runner should receive the configured timeout");
    expect_true(result.content.find("EXIT_CODE: 0") != std::string::npos,
                 "Command result should include the exit code");
    expect_true(result.content.find("[stdout]\nclean") != std::string::npos,
                "Command result should render stdout separately");
}

void expect_run_command_truncates_large_output() {
    ScopedTempDir temp_dir;
    auto runner = std::make_shared<RecordingCommandRunner>();
    runner->result = {true, false, 0, std::string(80, 'x'), ""};

    CommandPolicyConfig config;
    config.timeout_ms = 2500;
    config.max_output_bytes = 24;
    RunCommandTool tool(temp_dir.path(), runner, nullptr, config);
    const ToolResult result = tool.run("git status");

    expect_equal(result.success, true, "Truncated command output should still succeed");
    expect_true(runner->last_timeout_ms == 2500,
                "Custom command timeout should be forwarded to the runner");
    expect_true(result.content.find("[truncated ") != std::string::npos,
                "Large command output should be explicitly truncated");
    expect_true(result.content.find(std::string(80, 'x')) == std::string::npos,
                "Truncated command output should not keep the entire payload");
}

void expect_run_command_reports_timeout_and_stderr() {
    ScopedTempDir temp_dir;
    auto runner = std::make_shared<RecordingCommandRunner>();
    runner->result = {false, true, 124, "partial stdout", "timeout stderr"};

    RunCommandTool tool(temp_dir.path(), runner);
    const ToolResult result = tool.run("git status");

    expect_equal(result.success, false, "Timed out commands should fail");
    expect_true(result.content.find("TIMED_OUT: true") != std::string::npos,
                "Command result should report the timeout flag");
    expect_true(result.content.find("[stdout]\npartial stdout") != std::string::npos,
                "Command result should include stdout when present");
    expect_true(result.content.find("[stderr]\ntimeout stderr") != std::string::npos,
                "Command result should include stderr when present");
}

void expect_run_command_logs_execution_audit() {
    ScopedTempDir temp_dir;
    auto runner = std::make_shared<RecordingCommandRunner>();
    auto audit_logger = std::make_shared<RecordingAuditLogger>();
    runner->result = {true, false, 0, "clean", ""};

    RunCommandTool tool(temp_dir.path(), runner, audit_logger);
    const ToolResult result = tool.run("git status");

    expect_equal(result.success, true, "Whitelisted command should still succeed");
    expect_true(audit_logger->events.size() == 1,
                "Successful command execution should emit one audit event");
    expect_equal(audit_logger->events[0].stage, "executed",
                 "Audit event should record command execution");
    expect_equal(audit_logger->events[0].target, "git status",
                 "Audit event should preserve the executed command");
    expect_true(audit_logger->events[0].timeout_ms == CommandPolicyConfig{}.timeout_ms,
                "Audit event should record the configured timeout");
}

void expect_run_command_rejects_non_whitelisted_executable() {
    ScopedTempDir temp_dir;
    auto runner = std::make_shared<RecordingCommandRunner>();

    RunCommandTool tool(temp_dir.path(), runner);
    const ToolPreview preview = tool.preview("powershell Get-ChildItem");
    expect_true(preview.details.find("Executable is not in the whitelist") != std::string::npos,
                "Preview should surface whitelist failures");

    const ToolResult result = tool.run("powershell Get-ChildItem");
    expect_equal(result.success, false, "Non-whitelisted executable should fail");
    expect_true(result.content.find("Executable is not in the whitelist") != std::string::npos,
                "Run should reject non-whitelisted executables");
    expect_equal(runner->last_command, "", "Rejected commands should not reach the runner");
}

void expect_run_command_rejects_metacharacters() {
    ScopedTempDir temp_dir;
    auto runner = std::make_shared<RecordingCommandRunner>();

    RunCommandTool tool(temp_dir.path(), runner);
    // Pipes and redirects are now allowed for dev workflows.
    // But background execution (&) and chaining (;) are still blocked.
    const ToolResult result = tool.run("git status & echo pwned");
    expect_equal(result.success, false, "Dangerous metacharacters should be blocked");
    expect_true(result.content.find("blocked shell metacharacters") != std::string::npos,
                "Blocked metacharacter failure should be explicit");
}

void expect_agent_reads_file_then_replies() {
    ScopedTempDir temp_dir;
    const auto file_system = std::make_shared<LocalTestFileSystem>();
    const auto command_runner = std::make_shared<RecordingCommandRunner>();
    const auto approval_provider = std::make_shared<RecordingApprovalProvider>(
        std::vector<bool>{});

    write_text_file(temp_dir.path() / "notes.txt", "hello from file\n");

    auto scripted_client = std::make_unique<ScriptedModelClient>(std::vector<std::string>{
        R"({"action":"tool","tool":"read_file","args":"notes.txt","reason":"Need file contents","risk":"low","requires_confirmation":"false"})",
        R"({"action":"reply","content":"notes.txt says hello from file","reason":"Tool result is enough","risk":"low","requires_confirmation":"false"})",
    });
    ScriptedModelClient* client_ptr = scripted_client.get();

    Agent agent(std::move(scripted_client), approval_provider, temp_dir.path(), {}, {}, false,
                nullptr, make_test_tool_registry(temp_dir.path(), file_system, command_runner));
    const std::string output = agent.run_turn("What does notes.txt say?");

    expect_equal(output, "notes.txt says hello from file",
                 "Agent should complete a read-file tool round trip");
    expect_true(client_ptr->prompts().size() == 2,
                "Agent should ask the model twice for tool then reply");
    expect_true(client_ptr->prompts()[1].find("[tool:read_file]") != std::string::npos,
                "Second prompt should include the read_file tool history");
    expect_true(client_ptr->prompts()[1].find("TOOL_OK\nFILE: notes.txt\nhello from file") !=
                    std::string::npos,
                "Second prompt should include the successful file contents");
}

void expect_agent_records_multi_step_desktop_trace() {
    ScopedTempDir temp_dir;
    const auto file_system = std::make_shared<LocalTestFileSystem>();
    const auto command_runner = std::make_shared<RecordingCommandRunner>();
    const auto process_manager = std::make_shared<RecordingProcessManager>();
    const auto window_controller = std::make_shared<RecordingWindowController>();
    const auto ui_automation = std::make_shared<RecordingUiAutomation>();
    const auto approval_provider = std::make_shared<RecordingApprovalProvider>(
        std::vector<bool>{true, true, true});

    process_manager->launch_result = {true, 9001, ""};
    window_controller->list_result.windows = {
        {"0x777", 9001, "Untitled - Notepad", "Notepad", true},
    };
    window_controller->focus_result = {true,
                                       {"0x777", 9001, "Untitled - Notepad", "Notepad", true},
                                       ""};

    auto scripted_client = std::make_unique<ScriptedModelClient>(std::vector<std::string>{
        R"({"action":"tool","tool":"open_app","args":"notepad.exe","reason":"Need to open Notepad first","risk":"medium","requires_confirmation":"true"})",
        R"({"action":"tool","tool":"wait_for_window","args":"title=Notepad\ntimeout_ms=100","reason":"Need the Notepad window to exist before typing","risk":"low","requires_confirmation":"false"})",
        R"({"action":"tool","tool":"focus_window","args":"title=Untitled - Notepad","reason":"Need the target window in the foreground before typing","risk":"medium","requires_confirmation":"true"})",
        R"({"action":"tool","tool":"type_text","args":"text<<<\nhello from task runner\n>>>","reason":"Need to enter the requested text","risk":"medium","requires_confirmation":"true"})",
        R"({"action":"reply","content":"Completed the desktop task.","reason":"All requested steps finished","risk":"low","requires_confirmation":"false"})",
    });

    Agent agent(std::move(scripted_client), approval_provider, temp_dir.path(), {}, {}, false,
                nullptr,
                make_test_tool_registry(temp_dir.path(), file_system, command_runner, nullptr,
                                        process_manager, ui_automation, window_controller));
    const std::string output = agent.run_turn("Open Notepad and type hello from task runner.");

    expect_equal(output, "Completed the desktop task.",
                 "Agent should finish the planned desktop task");
    expect_equal(process_manager->last_command_line, "notepad.exe",
                 "Desktop task should launch the requested application");
    expect_equal(window_controller->last_target.title, "Untitled - Notepad",
                 "Desktop task should focus the requested window");
    expect_equal(ui_automation->last_text, "hello from task runner",
                 "Desktop task should send the requested text");
    expect_true(agent.last_execution_trace().size() == 4,
                "Task trace should record each executed desktop tool step");
    expect_true(agent.last_execution_trace()[0].tool_name == "open_app" &&
                    agent.last_execution_trace()[0].status == ExecutionStepStatus::completed,
                "First execution step should record the successful application launch");
    expect_true(agent.last_execution_trace()[3].tool_name == "type_text" &&
                    agent.last_execution_trace()[3].status == ExecutionStepStatus::completed,
                "Last execution step should record the successful text input");
}

void expect_agent_trace_observer_receives_progress_updates() {
    ScopedTempDir temp_dir;
    const auto file_system = std::make_shared<LocalTestFileSystem>();
    const auto command_runner = std::make_shared<RecordingCommandRunner>();
    const auto approval_provider = std::make_shared<RecordingApprovalProvider>(
        std::vector<bool>{});

    write_text_file(temp_dir.path() / "notes.txt", "hello from observer\n");

    auto scripted_client = std::make_unique<ScriptedModelClient>(std::vector<std::string>{
        R"({"action":"tool","tool":"read_file","args":"notes.txt","reason":"Need file contents","risk":"low","requires_confirmation":"false"})",
        R"({"action":"reply","content":"done","reason":"enough","risk":"low","requires_confirmation":"false"})",
    });

    Agent agent(std::move(scripted_client), approval_provider, temp_dir.path(), {}, {}, false,
                nullptr, make_test_tool_registry(temp_dir.path(), file_system, command_runner));

    std::vector<std::vector<ExecutionStep>> updates;
    agent.set_trace_observer([&updates](const std::vector<ExecutionStep>& trace) {
        updates.push_back(trace);
    });

    const std::string output = agent.run_turn("Read notes.txt.");
    expect_equal(output, "done", "Trace observer test should still finish the turn");
    expect_true(updates.size() >= 3,
                "Trace observer should receive at least clear, planned, and completed updates");
    expect_true(!updates[1].empty() &&
                    updates[1].back().status == ExecutionStepStatus::planned,
                "Trace observer should see the step in planned state before execution finishes");
    expect_true(!updates.back().empty() &&
                    updates.back().back().status == ExecutionStepStatus::completed,
                "Trace observer should see the final completed state");
}

void expect_agent_denied_approval_stops_execution() {
    ScopedTempDir temp_dir;
    const auto file_system = std::make_shared<LocalTestFileSystem>();
    const auto command_runner = std::make_shared<RecordingCommandRunner>();
    const auto approval_provider = std::make_shared<RecordingApprovalProvider>(
        std::vector<bool>{false});

    auto scripted_client = std::make_unique<ScriptedModelClient>(std::vector<std::string>{
        R"({"action":"tool","tool":"run_command","args":"ollama list","reason":"Need local model state","risk":"medium","requires_confirmation":"true"})",
    });
    ScriptedModelClient* client_ptr = scripted_client.get();

    Agent agent(std::move(scripted_client), approval_provider, temp_dir.path(), {}, {}, false,
                nullptr, make_test_tool_registry(temp_dir.path(), file_system, command_runner));
    const std::string output = agent.run_turn("Run ollama list.");

    expect_true(output.find("Approval denied for run_command") != std::string::npos,
                "Denied approval should stop the turn with an explicit message");
    expect_true(approval_provider->requests.size() == 1,
                "Approval provider should receive exactly one request");
    expect_equal(approval_provider->requests[0].tool_name, "run_command",
                 "Approval request should identify the tool");
    expect_equal(command_runner->last_command, "",
                 "Denied command should not reach the command runner");
    expect_true(client_ptr->prompts().size() == 1,
                "Denied approval should stop the loop without another model step");
}

void expect_agent_logs_command_audit_on_approval_denial() {
    ScopedTempDir temp_dir;
    const auto file_system = std::make_shared<LocalTestFileSystem>();
    const auto command_runner = std::make_shared<RecordingCommandRunner>();
    const auto approval_provider = std::make_shared<RecordingApprovalProvider>(
        std::vector<bool>{false});
    const auto audit_logger = std::make_shared<RecordingAuditLogger>();

    auto scripted_client = std::make_unique<ScriptedModelClient>(std::vector<std::string>{
        R"({"action":"tool","tool":"run_command","args":"ollama list","reason":"Need local model state","risk":"medium","requires_confirmation":"true"})",
    });

    Agent agent(std::move(scripted_client), approval_provider, temp_dir.path(), {}, {}, false,
                audit_logger,
                make_test_tool_registry(temp_dir.path(), file_system, command_runner,
                                        audit_logger));
    const std::string output = agent.run_turn("Run ollama list.");

    expect_true(output.find("Approval denied for run_command") != std::string::npos,
                "Approval denial should still stop the turn");
    expect_true(audit_logger->events.size() == 2,
                "Denied command should emit request and denial audit events");
    expect_equal(audit_logger->events[0].stage, "requested",
                 "First audit event should record the request");
    expect_equal(audit_logger->events[1].stage, "approval_denied",
                 "Second audit event should record approval denial");
}

void expect_agent_logs_write_file_audit_on_approval_denial() {
    ScopedTempDir temp_dir;
    const auto file_system = std::make_shared<LocalTestFileSystem>();
    const auto command_runner = std::make_shared<RecordingCommandRunner>();
    const auto approval_provider = std::make_shared<RecordingApprovalProvider>(
        std::vector<bool>{false});
    const auto audit_logger = std::make_shared<RecordingAuditLogger>();

    auto scripted_client = std::make_unique<ScriptedModelClient>(std::vector<std::string>{
        R"({"action":"tool","tool":"write_file","args":"path=notes.txt\ncontent<<<\nhello\n>>>","reason":"Need to create a note","risk":"medium","requires_confirmation":"true"})",
    });

    Agent agent(std::move(scripted_client), approval_provider, temp_dir.path(), {}, {}, false,
                audit_logger,
                make_test_tool_registry(temp_dir.path(), file_system, command_runner,
                                        audit_logger));
    const std::string output = agent.run_turn("Create notes.txt.");

    expect_true(output.find("Approval denied for write_file") != std::string::npos,
                "Approval denial should stop write_file execution");
    expect_true(audit_logger->events.size() == 2,
                "Denied write_file should emit request and denial audit events");
    expect_equal(audit_logger->events[0].tool_name, "write_file",
                 "First audit event should target write_file");
    expect_equal(audit_logger->events[0].target, "notes.txt",
                 "Write-file approval audit should record only the target path");
    expect_equal(audit_logger->events[0].stage, "requested",
                 "First audit event should record the request");
    expect_equal(audit_logger->events[1].tool_name, "write_file",
                 "Second audit event should target write_file");
    expect_equal(audit_logger->events[1].stage, "approval_denied",
                 "Second audit event should record approval denial");
}

void expect_agent_recovers_from_format_error() {
    ScopedTempDir temp_dir;
    const auto file_system = std::make_shared<LocalTestFileSystem>();
    const auto command_runner = std::make_shared<RecordingCommandRunner>();
    const auto approval_provider = std::make_shared<RecordingApprovalProvider>(
        std::vector<bool>{});

    auto scripted_client = std::make_unique<ScriptedModelClient>(std::vector<std::string>{
        "not json at all",
        R"({"action":"reply","content":"Recovered reply","reason":"Retried after format error","risk":"low","requires_confirmation":"false"})",
    });
    ScriptedModelClient* client_ptr = scripted_client.get();

    Agent agent(std::move(scripted_client), approval_provider, temp_dir.path(), {}, {}, false,
                nullptr, make_test_tool_registry(temp_dir.path(), file_system, command_runner));
    const std::string output = agent.run_turn("Say something.");

    expect_equal(output, "Recovered reply",
                 "Agent should retry after a format error and return the next valid reply");
    expect_true(client_ptr->prompts().size() == 2,
                "Format error recovery should trigger a second model step");
    expect_true(client_ptr->prompts()[1].find("FORMAT_ERROR:") != std::string::npos,
                "Retry prompt should include the format error feedback");
}

void expect_agent_respects_history_window() {
    ScopedTempDir temp_dir;
    const auto file_system = std::make_shared<LocalTestFileSystem>();
    const auto command_runner = std::make_shared<RecordingCommandRunner>();
    const auto approval_provider = std::make_shared<RecordingApprovalProvider>(
        std::vector<bool>{});

    auto scripted_client = std::make_unique<ScriptedModelClient>(std::vector<std::string>{
        R"({"action":"reply","content":"reply-one","reason":"done","risk":"low","requires_confirmation":"false"})",
        R"({"action":"reply","content":"reply-two","reason":"done","risk":"low","requires_confirmation":"false"})",
    });
    ScriptedModelClient* client_ptr = scripted_client.get();

    AgentRuntimeConfig runtime_config;
    runtime_config.history_window = 2;

    Agent agent(std::move(scripted_client), approval_provider, temp_dir.path(), {}, runtime_config,
                false, nullptr,
                make_test_tool_registry(temp_dir.path(), file_system, command_runner));
    (void)agent.run_turn("first-question-marker");
    (void)agent.run_turn("second-question-marker");

    expect_true(client_ptr->prompts().size() == 2,
                "Two agent turns should produce two model prompts");
    expect_true(client_ptr->prompts()[1].find("first-question-marker") == std::string::npos,
                "History window should trim older user messages from later prompts");
    expect_true(client_ptr->prompts()[1].find("reply-one") != std::string::npos,
                "History window should still include the most recent assistant reply");
    expect_true(client_ptr->prompts()[1].find("second-question-marker") != std::string::npos,
                "Later prompt should include the current user message");
}

void expect_agent_respects_max_model_steps() {
    ScopedTempDir temp_dir;
    const auto file_system = std::make_shared<LocalTestFileSystem>();
    const auto command_runner = std::make_shared<RecordingCommandRunner>();
    const auto approval_provider = std::make_shared<RecordingApprovalProvider>(
        std::vector<bool>{});

    auto scripted_client = std::make_unique<ScriptedModelClient>(std::vector<std::string>{
        "still not json",
    });

    AgentRuntimeConfig runtime_config;
    runtime_config.max_model_steps = 1;

    Agent agent(std::move(scripted_client), approval_provider, temp_dir.path(), {}, runtime_config,
                false, nullptr,
                make_test_tool_registry(temp_dir.path(), file_system, command_runner));

    bool threw = false;
    try {
        (void)agent.run_turn("trigger step limit");
    } catch (const std::runtime_error& error) {
        threw = std::string(error.what()).find("maximum number of model steps") !=
                std::string::npos;
    }
    expect_true(threw, "Agent should enforce the configured maximum model steps");
}

void expect_agent_respects_history_byte_budget() {
    ScopedTempDir temp_dir;
    const auto file_system = std::make_shared<LocalTestFileSystem>();
    const auto command_runner = std::make_shared<RecordingCommandRunner>();
    const auto approval_provider = std::make_shared<RecordingApprovalProvider>(
        std::vector<bool>{});

    auto scripted_client = std::make_unique<ScriptedModelClient>(std::vector<std::string>{
        R"({"action":"reply","content":"AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA","reason":"done","risk":"low","requires_confirmation":"false"})",
        R"({"action":"reply","content":"second reply","reason":"done","risk":"low","requires_confirmation":"false"})",
    });
    ScriptedModelClient* client_ptr = scripted_client.get();

    AgentRuntimeConfig runtime_config;
    runtime_config.history_window = 10;
    runtime_config.history_byte_budget = 90;

    Agent agent(std::move(scripted_client), approval_provider, temp_dir.path(), {}, runtime_config,
                false, nullptr,
                make_test_tool_registry(temp_dir.path(), file_system, command_runner));
    (void)agent.run_turn("first-question-with-extra-bytes");
    (void)agent.run_turn("second-question");

    expect_true(client_ptr->prompts().size() == 2,
                "Two agent turns should produce two model prompts");
    expect_true(client_ptr->prompts()[1].find("first-question-with-extra-bytes") == std::string::npos,
                "History byte budget should evict older oversized messages");
}

void expect_agent_reports_available_tool_names() {
    ScopedTempDir temp_dir;
    const auto file_system = std::make_shared<LocalTestFileSystem>();
    const auto command_runner = std::make_shared<RecordingCommandRunner>();
    const auto approval_provider =
        std::make_shared<RecordingApprovalProvider>(std::vector<bool>{});

    auto scripted_client = std::make_unique<ScriptedModelClient>(
        std::vector<std::string>{
            R"({"action":"reply","content":"unused","reason":"done","risk":"low","requires_confirmation":"false"})",
        });

    Agent agent(std::move(scripted_client), approval_provider, temp_dir.path(), {}, {}, false,
                nullptr, make_test_tool_registry(temp_dir.path(), file_system, command_runner));
    const std::vector<std::string> names = agent.available_tool_names();

    expect_true(std::find(names.begin(), names.end(), "read_file") != names.end(),
                "Agent should expose read_file in the available tool list");
    expect_true(std::find(names.begin(), names.end(), "run_command") != names.end(),
                "Agent should expose run_command in the available tool list");
    expect_true(std::find(names.begin(), names.end(), "calculator") != names.end(),
                "Agent should expose calculator in the available tool list");
}

void expect_agent_diagnostic_tool_runs_read_only_tool() {
    ScopedTempDir temp_dir;
    const auto file_system = std::make_shared<LocalTestFileSystem>();
    const auto command_runner = std::make_shared<RecordingCommandRunner>();
    const auto approval_provider =
        std::make_shared<RecordingApprovalProvider>(std::vector<bool>{});

    auto scripted_client = std::make_unique<ScriptedModelClient>(
        std::vector<std::string>{
            R"({"action":"reply","content":"unused","reason":"done","risk":"low","requires_confirmation":"false"})",
        });

    Agent agent(std::move(scripted_client), approval_provider, temp_dir.path(), {}, {}, false,
                nullptr, make_test_tool_registry(temp_dir.path(), file_system, command_runner));
    const ToolResult result = agent.run_diagnostic_tool("calculator", "2 + 3");

    expect_equal(result.success, true, "Diagnostic tool run should allow read-only tools");
    expect_equal(result.content, "5", "Diagnostic tool run should execute the target tool");
}

void expect_agent_diagnostic_tool_rejects_side_effect_tool() {
    ScopedTempDir temp_dir;
    const auto file_system = std::make_shared<LocalTestFileSystem>();
    const auto command_runner = std::make_shared<RecordingCommandRunner>();
    const auto approval_provider =
        std::make_shared<RecordingApprovalProvider>(std::vector<bool>{});

    auto scripted_client = std::make_unique<ScriptedModelClient>(
        std::vector<std::string>{
            R"({"action":"reply","content":"unused","reason":"done","risk":"low","requires_confirmation":"false"})",
        });

    Agent agent(std::move(scripted_client), approval_provider, temp_dir.path(), {}, {}, false,
                nullptr, make_test_tool_registry(temp_dir.path(), file_system, command_runner));
    const ToolResult result = agent.run_diagnostic_tool("run_command", "git status");

    expect_equal(result.success, false,
                 "Diagnostic tool run should reject side-effect tools");
    expect_true(result.content.find("Self-check cannot run") != std::string::npos,
                "Diagnostic tool rejection should explain the policy reason");
}

void expect_self_check_runner_reports_verified_safe_tools() {
    ScopedTempDir temp_dir;
    const auto file_system = std::make_shared<LocalTestFileSystem>();
    const auto command_runner = std::make_shared<RecordingCommandRunner>();
    const auto process_manager = std::make_shared<RecordingProcessManager>();
    const auto window_controller = std::make_shared<RecordingWindowController>();
    const auto ui_automation = std::make_shared<RecordingUiAutomation>();
    const auto approval_provider =
        std::make_shared<RecordingApprovalProvider>(std::vector<bool>{});

    write_text_file(temp_dir.path() / "src" / "main.cpp", "int main() { return 0; }\n");
    process_manager->list_result = {true, {{6560, "explorer.exe"}}, ""};
    window_controller->list_result = {true, {{"100", 6560, "任务栏", "Shell_TrayWnd", true}}, ""};
    ui_automation->inspect_result = {
        true, "100", "任务栏",
        {{"200", "100", "Button", "开始", true, true}},
        ""
    };

    auto scripted_client = std::make_unique<ScriptedModelClient>(
        std::vector<std::string>{
            R"({"action":"reply","content":"unused","reason":"done","risk":"low","requires_confirmation":"false"})",
        });

    Agent agent(std::move(scripted_client), approval_provider, temp_dir.path(), {}, {}, false,
                nullptr,
                make_test_tool_registry(temp_dir.path(), file_system, command_runner, nullptr,
                                        process_manager, ui_automation, window_controller));

    SelfCheckRunner runner(agent, temp_dir.path());
    const std::vector<CapabilityState> states = runner.run();

    const CapabilityState& calculator = find_capability_state(states, "calculator");
    const CapabilityState& write_file = find_capability_state(states, "write_file");
    const CapabilityState& inspect_ui = find_capability_state(states, "inspect_ui");

    expect_equal(calculator.verified, true,
                 "Self-check should verify safe read-only tools");
    expect_equal(calculator.level, "ok",
                 "Verified safe tools should report ok level");
    expect_equal(write_file.level, "untested",
                 "Self-check should leave side-effect tools untested");
    expect_equal(inspect_ui.verified, true,
                 "Self-check should verify inspect_ui when UI automation is available");
}

void expect_static_approval_provider_returns_configured_decision() {
    ApprovalRequest request;
    request.tool_name = "run_command";

    StaticApprovalProvider approve_all(true);
    StaticApprovalProvider deny_all(false);

    expect_equal(approve_all.approve(request), true,
                 "Static approval provider should allow when configured true");
    expect_equal(deny_all.approve(request), false,
                 "Static approval provider should deny when configured false");
}

void expect_cli_options_use_config_defaults() {
    AppConfig config;
    config.default_model = "cfg:model";
    config.agent_runtime.default_debug = true;

    const AgentCliOptions options = resolve_agent_cli_options({}, config);
    expect_equal(options.model, "cfg:model",
                 "CLI resolution should fall back to the configured model");
    expect_equal(options.debug, true,
                 "CLI resolution should fall back to configured debug default");
    expect_equal(options.prompt, "", "CLI resolution should default to an empty prompt");
}

void expect_cli_options_allow_explicit_overrides() {
    AppConfig config;
    config.default_model = "cfg:model";
    config.agent_runtime.default_debug = false;

    const AgentCliOptions options = resolve_agent_cli_options(
        {"--debug", "--model", "cli:model", "Search", "the", "workspace"}, config);
    expect_equal(options.model, "cli:model",
                 "Explicit --model should override the configured model");
    expect_equal(options.debug, true, "Explicit --debug should override config");
    expect_equal(options.prompt, "Search the workspace",
                 "CLI resolution should join prompt tokens");
}

void expect_cli_options_support_legacy_positional_model() {
    AppConfig config;
    config.default_model = "cfg:model";
    config.agent_runtime.default_debug = false;

    const AgentCliOptions options =
        resolve_agent_cli_options({"qwen3:8b", "Read", "src/main.cpp"}, config);
    expect_equal(options.model, "qwen3:8b",
                 "Legacy positional model should still be accepted");
    expect_equal(options.prompt, "Read src/main.cpp",
                 "Prompt should begin after the positional model");
}

void expect_cli_options_reject_unknown_option() {
    AppConfig config;

    bool threw = false;
    try {
        (void)resolve_agent_cli_options({"--bogus"}, config);
    } catch (const std::invalid_argument& error) {
        threw = std::string(error.what()).find("Unknown agent option") != std::string::npos;
    }
    expect_true(threw, "Unknown CLI options should be rejected");
}

void expect_cli_options_require_model_value() {
    AppConfig config;

    bool threw = false;
    try {
        (void)resolve_agent_cli_options({"--model"}, config);
    } catch (const std::invalid_argument& error) {
        threw = std::string(error.what()).find("--model requires a value") !=
                std::string::npos;
    }
    expect_true(threw, "Missing --model value should be rejected");
}

void expect_web_chat_options_use_config_defaults() {
    AppConfig config;
    config.default_model = "cfg:model";
    config.agent_runtime.default_debug = true;

    const WebChatCliOptions options = resolve_web_chat_cli_options({}, config);
    expect_equal(options.model, "cfg:model",
                 "web-chat options should default to the configured model");
    expect_equal(options.debug, true,
                 "web-chat options should default to the configured debug flag");
    expect_true(options.port == 8080,
                "web-chat options should default to port 8080");
}

void expect_web_chat_options_allow_explicit_overrides() {
    AppConfig config;
    config.default_model = "cfg:model";
    config.agent_runtime.default_debug = false;

    const WebChatCliOptions options =
        resolve_web_chat_cli_options({"--model", "qwen3:8b", "--debug", "--port", "9090"},
                                     config);
    expect_equal(options.model, "qwen3:8b",
                 "web-chat options should honor explicit model overrides");
    expect_equal(options.debug, true,
                 "web-chat options should honor explicit debug overrides");
    expect_true(options.port == 9090,
                "web-chat options should honor explicit port overrides");
}

void expect_web_chat_options_reject_unknown_option() {
    AppConfig config;

    bool threw = false;
    try {
        (void)resolve_web_chat_cli_options({"--bogus"}, config);
    } catch (const std::invalid_argument& error) {
        threw = std::string(error.what()).find("Unknown web-chat option") !=
                std::string::npos;
    }
    expect_true(threw, "Unknown web-chat options should be rejected");
}

void expect_program_cli_resolves_usage_when_no_command() {
    const TopLevelCommand command = resolve_top_level_command({});
    expect_true(command.type == TopLevelCommandType::usage,
                "Missing top-level command should resolve to usage");
}

void expect_program_cli_resolves_train_demo() {
    const TopLevelCommand command = resolve_top_level_command({"train-demo"});
    expect_true(command.type == TopLevelCommandType::train_demo,
                "train-demo should resolve to the training command");
}

void expect_program_cli_resolves_agent() {
    const TopLevelCommand command = resolve_top_level_command({"agent", "--debug"});
    expect_true(command.type == TopLevelCommandType::agent,
                "agent should resolve to the agent command");
}

void expect_program_cli_resolves_web_chat() {
    const TopLevelCommand command = resolve_top_level_command({"web-chat", "--port", "8080"});
    expect_true(command.type == TopLevelCommandType::web_chat,
                "web-chat should resolve to the web chat command");
}

void expect_program_cli_rejects_unknown_command() {
    const TopLevelCommand command = resolve_top_level_command({"unknown"});
    expect_true(command.type == TopLevelCommandType::invalid,
                "Unknown top-level command should resolve as invalid");
    expect_equal(command.command, "unknown",
                 "Invalid command resolution should preserve the bad token");
}

void expect_program_cli_builds_usage_text() {
    const std::string usage = build_usage_text("bolt");
    expect_true(usage.find("bolt train-demo") != std::string::npos,
                "Usage text should include the training command");
    expect_true(usage.find("agent [--debug|--no-debug] [--model <model>|<model>] [prompt]") !=
                    std::string::npos,
                "Usage text should include the agent syntax");
    expect_true(usage.find("web-chat [--debug|--no-debug] [--model <model>|<model>] [--port <port>]") !=
                    std::string::npos,
                "Usage text should include the web-chat syntax");
    expect_true(usage.find("agent --debug qwen3:8b Search the workspace for ToolRegistry.") !=
                    std::string::npos,
                "Usage text should include the agent example");
}

void expect_agent_runner_single_turn_writes_response() {
    ScopedTempDir temp_dir;
    const auto file_system = std::make_shared<LocalTestFileSystem>();
    const auto command_runner = std::make_shared<RecordingCommandRunner>();
    const auto approval_provider = std::make_shared<RecordingApprovalProvider>(
        std::vector<bool>{});

    auto scripted_client = std::make_unique<ScriptedModelClient>(std::vector<std::string>{
        R"({"action":"reply","content":"single-turn reply","reason":"done","risk":"low","requires_confirmation":"false"})",
    });

    Agent agent(std::move(scripted_client), approval_provider, temp_dir.path(), {}, {}, false,
                nullptr, make_test_tool_registry(temp_dir.path(), file_system, command_runner));
    std::ostringstream output;

    const int exit_code = run_agent_single_turn(agent, "Say something.", output);
    expect_true(exit_code == 0, "Single-turn runner should return success");
    expect_equal(output.str(), "single-turn reply\n",
                 "Single-turn runner should write the agent response");
}

void expect_agent_runner_interactive_loop_handles_commands() {
    ScopedTempDir temp_dir;
    const auto file_system = std::make_shared<LocalTestFileSystem>();
    const auto command_runner = std::make_shared<RecordingCommandRunner>();
    const auto approval_provider = std::make_shared<RecordingApprovalProvider>(
        std::vector<bool>{});

    auto scripted_client = std::make_unique<ScriptedModelClient>(std::vector<std::string>{
        R"({"action":"reply","content":"first reply","reason":"done","risk":"low","requires_confirmation":"false"})",
        R"({"action":"reply","content":"second reply","reason":"done","risk":"low","requires_confirmation":"false"})",
    });
    ScriptedModelClient* client_ptr = scripted_client.get();

    Agent agent(std::move(scripted_client), approval_provider, temp_dir.path(), {}, {}, false,
                nullptr, make_test_tool_registry(temp_dir.path(), file_system, command_runner));
    std::istringstream input("first-question\n/clear\n\nsecond-question\n/quit\n");
    std::ostringstream output;

    const int exit_code = run_agent_interactive_loop(agent, input, output);
    expect_true(exit_code == 0, "Interactive runner should return success");
    expect_true(output.str().find("Agent mode. Model: test-model") != std::string::npos,
                "Interactive runner should print the banner");
    expect_true(output.str().find("first reply") != std::string::npos,
                "Interactive runner should print the first reply");
    expect_true(output.str().find("History cleared.") != std::string::npos,
                "Interactive runner should acknowledge /clear");
    expect_true(output.str().find("second reply") != std::string::npos,
                "Interactive runner should print the second reply");
    expect_true(client_ptr->prompts().size() == 2,
                "Interactive runner should only invoke the model for non-command inputs");
    expect_true(client_ptr->prompts()[1].find("first-question") == std::string::npos,
                "Clearing history should remove prior conversation before the next turn");
}

void expect_agent_factory_uses_supplied_services() {
    ScopedTempDir temp_dir;
    AppConfig config;
    config.agent_runtime.default_debug = false;

    AgentCliOptions options;
    options.model = "factory:model";
    options.debug = false;

    AgentServices services;
    services.file_system = std::make_shared<LocalTestFileSystem>();
    services.command_runner = std::make_shared<RecordingCommandRunner>();
    services.approval_provider = std::make_shared<RecordingApprovalProvider>(
        std::vector<bool>{});
    services.model_client = std::make_unique<ScriptedModelClient>(
        std::vector<std::string>{
            R"({"action":"reply","content":"factory reply","reason":"done","risk":"low","requires_confirmation":"false"})",
        },
        "factory:model");

    std::unique_ptr<Agent> agent =
        create_agent(temp_dir.path(), config, options, std::move(services));

    expect_equal(agent->model(), "factory:model",
                 "Agent factory should keep the supplied model client");
    expect_equal(agent->debug_enabled(), false,
                 "Agent factory should apply CLI debug overrides");
    expect_equal(agent->run_turn("test"), "factory reply",
                 "Agent factory should produce a working agent instance");
}

void expect_tool_registry_rejects_duplicate_registration() {
    ToolRegistry registry;
    registry.register_tool(std::make_unique<CalculatorTool>());

    bool threw = false;
    try {
        registry.register_tool(std::make_unique<CalculatorTool>());
    } catch (const std::invalid_argument& error) {
        threw = std::string(error.what()).find("Tool already registered") != std::string::npos;
    }

    expect_true(threw, "ToolRegistry should reject duplicate tool names");
}

void expect_default_tool_set_factory_registers_known_tools() {
    ScopedTempDir temp_dir;
    const auto file_system = std::make_shared<LocalTestFileSystem>();
    const auto command_runner = std::make_shared<RecordingCommandRunner>();
    const auto audit_logger = std::make_shared<RecordingAuditLogger>();
    const auto process_manager = std::make_shared<RecordingProcessManager>();
    const auto ui_automation = std::make_shared<RecordingUiAutomation>();
    const auto window_controller = std::make_shared<RecordingWindowController>();

    ToolRegistry registry = create_default_tool_registry(temp_dir.path(), file_system,
                                                         command_runner, audit_logger, {},
                                                         process_manager, ui_automation,
                                                         window_controller);

    expect_true(registry.find("read_file") != nullptr,
                "Default tool set should include read_file");
    expect_true(registry.find("run_command") != nullptr,
                "Default tool set should include run_command");
    expect_true(registry.find("calculator") != nullptr,
                "Default tool set should include calculator");
    expect_true(registry.find("list_processes") != nullptr,
                "Default tool set should include list_processes when a process manager is available");
    expect_true(registry.find("list_windows") != nullptr,
                "Default tool set should include list_windows when a window controller is available");
    expect_true(registry.find("wait_for_window") != nullptr,
                "Default tool set should include wait_for_window when a window controller is available");
    expect_true(registry.find("inspect_ui") != nullptr,
                "Default tool set should include inspect_ui when UI automation is available");
    expect_true(registry.find("click_element") != nullptr,
                "Default tool set should include click_element when UI automation is available");
    expect_true(registry.find("type_text") != nullptr,
                "Default tool set should include type_text when UI automation is available");
}

void expect_agent_factory_requires_approval_provider() {
    ScopedTempDir temp_dir;
    AppConfig config;

    AgentCliOptions options;
    options.model = "factory:model";

    AgentServices services;
    services.file_system = std::make_shared<LocalTestFileSystem>();
    services.command_runner = std::make_shared<RecordingCommandRunner>();
    services.model_client = std::make_unique<ScriptedModelClient>(
        std::vector<std::string>{
            R"({"action":"reply","content":"factory reply","reason":"done","risk":"low","requires_confirmation":"false"})",
        },
        "factory:model");

    bool threw = false;
    try {
        (void)create_agent(temp_dir.path(), config, options, std::move(services));
    } catch (const std::invalid_argument& error) {
        threw = std::string(error.what()).find("approval provider") != std::string::npos;
    }

    expect_true(threw, "Agent factory should reject missing approval providers");
}

void expect_agent_requires_tool_registry() {
    ScopedTempDir temp_dir;
    const auto approval_provider =
        std::make_shared<RecordingApprovalProvider>(std::vector<bool>{});
    auto scripted_client = std::make_unique<ScriptedModelClient>(
        std::vector<std::string>{
            R"({"action":"reply","content":"unused","reason":"done","risk":"low","requires_confirmation":"false"})",
        },
        "factory:model");

    bool threw = false;
    try {
        (void)Agent(std::move(scripted_client), approval_provider, temp_dir.path());
    } catch (const std::invalid_argument& error) {
        threw = std::string(error.what()).find("tool registry") != std::string::npos;
    }

    expect_true(threw, "Agent should reject missing tool registries");
}

#ifdef _WIN32
void expect_windows_agent_factory_uses_resolved_options() {
    ScopedTempDir temp_dir;
    AppConfig config;
    config.default_model = "cfg:model";
    config.agent_runtime.default_debug = false;
    config.approval.mode = ApprovalMode::auto_deny;

    AgentCliOptions options;
    options.model = "factory:model";
    options.debug = true;

    std::istringstream input;
    std::ostringstream output;
    std::unique_ptr<Agent> agent =
        create_windows_agent(temp_dir.path(), config, options, input, output);

    expect_equal(agent->model(), "factory:model",
                 "Windows agent factory should use resolved CLI model");
    expect_equal(agent->debug_enabled(), true,
                 "Windows agent factory should use resolved CLI debug flag");
}
#endif  // _WIN32

void expect_platform_agent_factory_reports_platform_name() {
#ifdef _WIN32
    expect_equal(current_platform_name(), "windows",
                 "Platform factory should report windows on Windows builds");
#elif defined(__APPLE__)
    expect_equal(current_platform_name(), "macos",
                 "Platform factory should report macos on Apple builds");
#elif defined(__linux__)
    expect_equal(current_platform_name(), "linux",
                 "Platform factory should report linux on Linux builds");
#else
    expect_equal(current_platform_name(), "unknown",
                 "Platform factory should report unknown on unsupported builds");
#endif
}

void expect_platform_agent_factory_uses_current_platform() {
#ifdef _WIN32
    ScopedTempDir temp_dir;
    AppConfig config;
    config.default_model = "cfg:model";
    config.agent_runtime.default_debug = false;
    config.approval.mode = ApprovalMode::auto_deny;

    AgentCliOptions options;
    options.model = "platform:model";
    options.debug = true;

    std::istringstream input;
    std::ostringstream output;
    std::unique_ptr<Agent> agent =
        create_platform_agent(temp_dir.path(), config, options, input, output);

    expect_equal(agent->model(), "platform:model",
                 "Platform agent factory should use the current platform model client");
    expect_equal(agent->debug_enabled(), true,
                 "Platform agent factory should preserve resolved debug options");
#else
    bool threw = false;
    try {
        AppConfig config;
        AgentCliOptions options;
        std::istringstream input;
        std::ostringstream output;
        (void)create_platform_agent(std::filesystem::current_path(), config, options, input,
                                    output);
    } catch (const std::runtime_error& error) {
        threw = std::string(error.what()).find("No platform agent factory is implemented") !=
                std::string::npos;
    }
    expect_true(threw,
                "Unsupported platform builds should fail with an explicit factory error");
#endif
}

}  // namespace

int main() {
    const std::vector<std::pair<std::string, std::function<void()>>> tests = {
        {"action reply parses with metadata", expect_action_reply_parses_with_metadata},
        {"action tool parses escaped args", expect_action_tool_parses_escaped_args},
        {"action parser decodes unicode escape sequences",
         expect_action_parser_decodes_unicode_escape_sequences},
        {"action parser rejects missing action", expect_action_parser_rejects_missing_action},
        {"policy allows read without approval", expect_policy_allows_read_without_approval},
        {"policy allows desktop read without approval",
         expect_policy_allows_desktop_read_without_approval},
        {"policy ignores confirmation flag for read-only tool",
         expect_policy_ignores_confirmation_flag_for_read_only_tool},
        {"policy requires approval for write", expect_policy_requires_approval_for_write},
        {"policy requires approval for desktop control",
         expect_policy_requires_approval_for_desktop_control},
        {"policy blocks high risk action", expect_policy_blocks_high_risk_action},
        {"policy config can disable high risk block",
         expect_policy_config_can_disable_high_risk_block},
        {"policy config can reclassify tool bucket",
         expect_policy_config_can_reclassify_tool_bucket},
#ifdef _WIN32
        {"ollama json escapes control chars", expect_ollama_json_escape_preserves_control_chars},
        {"ollama json extracts top-level string", expect_ollama_json_extracts_top_level_string},
        {"ollama json handles escaped strings", expect_ollama_json_handles_escaped_strings},
        {"ollama json returns empty for non-string field",
         expect_ollama_json_returns_empty_for_non_string_field},
        {"ollama json rejects invalid json", expect_ollama_json_rejects_invalid_json},
#endif
        {"app config reads workspace file", expect_app_config_reads_workspace_file},
        {"edit_file exact replace updates file", expect_edit_file_exact_replace_updates_file},
        {"edit_file line patch updates file", expect_edit_file_line_patch_updates_file},
        {"write_file logs execution audit", expect_write_file_logs_execution_audit},
        {"edit_file logs execution audit", expect_edit_file_logs_execution_audit},
        {"edit_file rejects ambiguous match", expect_edit_file_rejects_ambiguous_match},
        {"list_processes formats processes", expect_list_processes_formats_processes},
        {"list_windows formats windows", expect_list_windows_formats_windows},
        {"open_app logs execution audit", expect_open_app_logs_execution_audit},
        {"focus_window logs execution audit", expect_focus_window_logs_execution_audit},
        {"wait_for_window finds matching window", expect_wait_for_window_finds_matching_window},
        {"type_text logs execution audit", expect_type_text_logs_execution_audit},
        {"inspect_ui formats elements", expect_inspect_ui_formats_elements},
        {"click_element logs execution audit", expect_click_element_logs_execution_audit},
        {"run_command executes allowed command", expect_run_command_executes_allowed_command},
        {"run_command truncates large output", expect_run_command_truncates_large_output},
        {"run_command reports timeout and stderr",
         expect_run_command_reports_timeout_and_stderr},
        {"run_command logs execution audit", expect_run_command_logs_execution_audit},
        {"run_command rejects non-whitelisted executable",
         expect_run_command_rejects_non_whitelisted_executable},
        {"run_command rejects metacharacters", expect_run_command_rejects_metacharacters},
        {"agent reads file then replies", expect_agent_reads_file_then_replies},
        {"agent records multi-step desktop trace", expect_agent_records_multi_step_desktop_trace},
        {"agent trace observer receives progress updates",
         expect_agent_trace_observer_receives_progress_updates},
        {"agent denied approval stops execution", expect_agent_denied_approval_stops_execution},
        {"agent logs command audit on approval denial",
         expect_agent_logs_command_audit_on_approval_denial},
        {"agent logs write_file audit on approval denial",
         expect_agent_logs_write_file_audit_on_approval_denial},
        {"agent recovers from format error", expect_agent_recovers_from_format_error},
        {"agent respects history window", expect_agent_respects_history_window},
        {"agent respects history byte budget", expect_agent_respects_history_byte_budget},
        {"agent respects max model steps", expect_agent_respects_max_model_steps},
        {"agent reports available tool names", expect_agent_reports_available_tool_names},
        {"agent diagnostic tool runs read-only tool",
         expect_agent_diagnostic_tool_runs_read_only_tool},
        {"agent diagnostic tool rejects side-effect tool",
         expect_agent_diagnostic_tool_rejects_side_effect_tool},
        {"self-check runner reports verified safe tools",
         expect_self_check_runner_reports_verified_safe_tools},
        {"static approval provider returns configured decision",
         expect_static_approval_provider_returns_configured_decision},
        {"cli options use config defaults", expect_cli_options_use_config_defaults},
        {"cli options allow explicit overrides", expect_cli_options_allow_explicit_overrides},
        {"cli options support legacy positional model",
         expect_cli_options_support_legacy_positional_model},
        {"cli options reject unknown option", expect_cli_options_reject_unknown_option},
        {"cli options require model value", expect_cli_options_require_model_value},
        {"web chat options use config defaults", expect_web_chat_options_use_config_defaults},
        {"web chat options allow explicit overrides",
         expect_web_chat_options_allow_explicit_overrides},
        {"web chat options reject unknown option",
         expect_web_chat_options_reject_unknown_option},
        {"program cli resolves usage when no command",
         expect_program_cli_resolves_usage_when_no_command},
        {"program cli resolves train demo", expect_program_cli_resolves_train_demo},
        {"program cli resolves agent", expect_program_cli_resolves_agent},
        {"program cli resolves web chat", expect_program_cli_resolves_web_chat},
        {"program cli rejects unknown command", expect_program_cli_rejects_unknown_command},
        {"program cli builds usage text", expect_program_cli_builds_usage_text},
        {"agent runner single turn writes response",
         expect_agent_runner_single_turn_writes_response},
        {"agent runner interactive loop handles commands",
         expect_agent_runner_interactive_loop_handles_commands},
        {"tool registry rejects duplicate registration",
         expect_tool_registry_rejects_duplicate_registration},
        {"default tool set factory registers known tools",
         expect_default_tool_set_factory_registers_known_tools},
        {"agent factory uses supplied services", expect_agent_factory_uses_supplied_services},
        {"agent factory requires approval provider",
         expect_agent_factory_requires_approval_provider},
        {"agent requires tool registry", expect_agent_requires_tool_registry},
        {"platform agent factory reports platform name",
         expect_platform_agent_factory_reports_platform_name},
        {"platform agent factory uses current platform",
         expect_platform_agent_factory_uses_current_platform},
#ifdef _WIN32
        {"windows agent factory uses resolved options",
         expect_windows_agent_factory_uses_resolved_options},
#endif
    };

    std::size_t passed = 0;
    for (const auto& test : tests) {
        try {
            test.second();
            ++passed;
            std::cout << "[PASS] " << test.first << '\n';
        } catch (const std::exception& error) {
            std::cerr << "[FAIL] " << test.first << ": " << error.what() << '\n';
            return 1;
        }
    }

    std::cout << "Passed " << passed << " kernel tests.\n";
    return 0;
}
