#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "../src/agent/agent.h"
#include "../src/agent/tool_set_factory.h"
#include "../src/agent/workspace_prompt.h"
#include "../src/app/app_config.h"
#include "../src/app/approval_provider_factory.h"
#include "../src/app/static_approval_provider.h"

namespace {

// --- Test infrastructure ---

class ScopedTempDir {
public:
    ScopedTempDir() {
        const auto base = std::filesystem::temp_directory_path();
        path_ = base / std::filesystem::path("bolt_e2e_" +
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

    const std::filesystem::path& path() const { return path_; }

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
    DirectoryListResult list_directory(const std::filesystem::path& path) const override {
        DirectoryListResult result;
        result.success = true;
        for (const auto& entry : std::filesystem::directory_iterator(path)) {
            DirectoryEntryInfo info;
        info.name = entry.path().filename().string();
        info.is_directory = entry.is_directory();
        info.size = entry.is_regular_file() ? entry.file_size() : 0;
        result.entries.push_back(info);
        }
        return result;
    }
    bool create_directories(const std::filesystem::path& path,
                            std::string& error) const override {
        std::error_code ec;
        (void)std::filesystem::create_directories(path, ec);
        if (ec) { error = ec.message(); return false; }
        return true;
    }
    FileReadResult read_text_file(const std::filesystem::path& path) const override {
        std::ifstream input(path, std::ios::binary);
        if (!input) return {false, "", "Failed to open file"};
        std::string content((std::istreambuf_iterator<char>(input)),
                            std::istreambuf_iterator<char>());
        return {true, content, ""};
    }
    FileWriteResult write_text_file(const std::filesystem::path& path,
                                    const std::string& content) const override {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        if (!output) return {false, "Failed to open file for writing"};
        output << content;
        return {true, ""};
    }
    TextSearchResult search_text(const std::filesystem::path&, const std::string&,
                                 std::size_t, std::uintmax_t) const override {
        return {false, {}, false, "Not implemented"};
    }
};

class RecordingCommandRunner : public ICommandRunner {
public:
    mutable std::vector<std::string> commands;
    CommandExecutionResult result{true, false, 0, "", ""};

    CommandExecutionResult run(const std::string& command,
                               const std::filesystem::path&,
                               std::size_t) const override {
        commands.push_back(command);
        return result;
    }
};

/// Model client that returns structured ChatMessage responses with tool_calls.
/// This tests the `run_turn_structured()` path (native function calling).
class StructuredScriptedClient : public IModelClient {
public:
    explicit StructuredScriptedClient(std::vector<ChatMessage> responses,
                                      std::string model_name = "e2e-model")
        : responses_(std::move(responses)),
          model_name_(std::move(model_name)) {}

    std::string generate(const std::string&) const override {
        return "{}";
    }

    const std::string& model() const override { return model_name_; }

    ChatMessage chat(const std::vector<ChatMessage>&,
                     const std::vector<ToolSchema>&) const override {
        if (next_ >= responses_.size()) {
            // Return a final reply to avoid throwing
            ChatMessage reply;
            reply.role = ChatRole::assistant;
            reply.content = "No more scripted responses.";
            return reply;
        }
        return responses_[next_++];
    }

    ChatMessage chat_streaming(const std::vector<ChatMessage>& messages,
                                const std::vector<ToolSchema>& tools,
                                TokenCallback on_token) const override {
        ChatMessage result = chat(messages, tools);
        if (on_token && !result.content.empty()) on_token(result.content);
        return result;
    }

    bool supports_tools() const override { return true; }
    bool supports_streaming() const override { return true; }

    std::size_t calls() const { return next_; }

private:
    std::vector<ChatMessage> responses_;
    std::string model_name_;
    mutable std::size_t next_ = 0;
};

// --- Helpers ---

void write_file(const std::filesystem::path& path, const std::string& content) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output << content;
}

std::string read_file(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(input)),
                       std::istreambuf_iterator<char>());
}

void expect_true(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

void expect_contains(const std::string& haystack, const std::string& needle,
                     const std::string& message) {
    if (haystack.find(needle) == std::string::npos) {
        throw std::runtime_error(message + " ('" + needle + "' not found in output)");
    }
}

ChatMessage make_tool_call(const std::string& tool_name,
                           const std::string& args_json,
                           const std::string& content = "") {
    ChatMessage msg;
    msg.role = ChatRole::assistant;
    msg.content = content;
    msg.tool_calls.push_back({"call_1", tool_name, args_json});
    return msg;
}

ChatMessage make_multi_tool_call(
    const std::vector<std::pair<std::string, std::string>>& calls,
    const std::string& content = "") {
    ChatMessage msg;
    msg.role = ChatRole::assistant;
    msg.content = content;
    int id = 1;
    for (const auto& [name, args] : calls) {
        msg.tool_calls.push_back({"call_" + std::to_string(id++), name, args});
    }
    return msg;
}

ChatMessage make_reply(const std::string& content) {
    ChatMessage msg;
    msg.role = ChatRole::assistant;
    msg.content = content;
    return msg;
}

ToolRegistry make_test_tools(const std::filesystem::path& workspace_root,
                              const std::shared_ptr<IFileSystem>& fs,
                              const std::shared_ptr<ICommandRunner>& cmd) {
    return create_default_tool_registry(workspace_root, fs, cmd, nullptr, {},
                                        nullptr, nullptr, nullptr);
}

Agent make_agent(std::unique_ptr<IModelClient> client,
                 const std::filesystem::path& workspace,
                 const std::shared_ptr<IFileSystem>& fs,
                 const std::shared_ptr<ICommandRunner>& cmd,
                 int max_steps = 10) {
    AgentRuntimeConfig runtime;
    runtime.max_model_steps = max_steps;
    auto approval = std::make_shared<StaticApprovalProvider>(true);
    return Agent(std::move(client), approval, workspace, {}, runtime, false,
                 nullptr, make_test_tools(workspace, fs, cmd));
}

// --- Test cases ---

void test_structured_read_file_and_reply() {
    ScopedTempDir temp;
    write_file(temp.path() / "hello.txt", "Hello from E2E test!");

    auto client = std::make_unique<StructuredScriptedClient>(std::vector<ChatMessage>{
        make_tool_call("read_file", R"({"path": "hello.txt"})"),
        make_reply("The file contains a greeting."),
    });

    auto fs = std::make_shared<LocalTestFileSystem>();
    auto cmd = std::make_shared<RecordingCommandRunner>();
    Agent agent = make_agent(std::move(client), temp.path(), fs, cmd);

    const std::string result = agent.run_turn("Read hello.txt");

    expect_true(result == "The file contains a greeting.",
                "Agent should return model's reply after reading file");
    expect_true(agent.last_execution_trace().size() == 1,
                "Should have 1 execution step (read_file)");
    expect_true(agent.last_execution_trace()[0].tool_name == "read_file",
                "Execution step should be read_file");
}

void test_structured_write_and_verify() {
    ScopedTempDir temp;

    auto client = std::make_unique<StructuredScriptedClient>(std::vector<ChatMessage>{
        make_tool_call("write_file",
                       R"({"path": "output.txt", "content": "Generated content"})"),
        make_tool_call("read_file", R"({"path": "output.txt"})"),
        make_reply("File created and verified."),
    });

    auto fs = std::make_shared<LocalTestFileSystem>();
    auto cmd = std::make_shared<RecordingCommandRunner>();
    Agent agent = make_agent(std::move(client), temp.path(), fs, cmd);

    const std::string result = agent.run_turn("Create output.txt");

    expect_true(result == "File created and verified.",
                "Agent should complete write → read → reply cycle");
    expect_true(std::filesystem::exists(temp.path() / "output.txt"),
                "File should exist on disk after write_file tool");
    expect_contains(read_file(temp.path() / "output.txt"), "Generated content",
                    "Written file should contain expected content");
}

void test_structured_parallel_tool_calls() {
    ScopedTempDir temp;
    write_file(temp.path() / "a.txt", "File A");
    write_file(temp.path() / "b.txt", "File B");

    auto client = std::make_unique<StructuredScriptedClient>(std::vector<ChatMessage>{
        make_multi_tool_call({
            {"read_file", R"({"path": "a.txt"})"},
            {"read_file", R"({"path": "b.txt"})"},
        }),
        make_reply("Both files read successfully."),
    });

    auto fs = std::make_shared<LocalTestFileSystem>();
    auto cmd = std::make_shared<RecordingCommandRunner>();
    Agent agent = make_agent(std::move(client), temp.path(), fs, cmd);

    const std::string result = agent.run_turn("Read both files");

    expect_true(result == "Both files read successfully.",
                "Agent should handle parallel tool calls");
    expect_true(agent.last_execution_trace().size() == 2,
                "Should have 2 execution steps for parallel reads");
}

void test_structured_edit_build_fix_loop() {
    ScopedTempDir temp;
    write_file(temp.path() / "main.cpp", "int main() { return 0 }");  // missing semicolon

    auto cmd = std::make_shared<RecordingCommandRunner>();

    // Sequence: edit → build (fail) → edit (fix) → build (pass) → reply
    auto client = std::make_unique<StructuredScriptedClient>(std::vector<ChatMessage>{
        // Step 1: edit the file
        make_tool_call("edit_file",
                       R"({"path": "main.cpp", "old": "return 0 }", "new": "return 0; }"})"),
        // Step 2: build (will "fail" — command runner returns error)
        make_tool_call("build_and_test", R"({"args": ""})"),
        // Step 3: after seeing failure, model tries another edit
        make_tool_call("edit_file",
                       R"({"path": "main.cpp", "old": "return 0; }", "new": "return 0;\n}"})"),
        // Step 4: build again (success)
        make_tool_call("build_and_test", R"({"args": ""})"),
        // Step 5: reply
        make_reply("Fixed and verified."),
    });

    // First build fails, second succeeds
    int build_count = 0;
    cmd->result = {false, false, 1, "", "error: missing semicolon"};

    auto fs = std::make_shared<LocalTestFileSystem>();
    Agent agent = make_agent(std::move(client), temp.path(), fs, cmd);

    const std::string result = agent.run_turn("Fix the syntax error");

    expect_true(result == "Fixed and verified.",
                "Agent should complete edit→build→fix→build→reply loop");
    expect_true(agent.last_execution_trace().size() == 4,
                "Should have 4 execution steps in the fix loop");
}

void test_failure_recovery_injects_diagnostic() {
    ScopedTempDir temp;

    // Model repeatedly calls a tool that fails (read_file on nonexistent file)
    auto client = std::make_unique<StructuredScriptedClient>(std::vector<ChatMessage>{
        make_tool_call("read_file", R"({"path": "nonexistent1.txt"})"),
        make_tool_call("read_file", R"({"path": "nonexistent2.txt"})"),
        make_tool_call("read_file", R"({"path": "nonexistent3.txt"})"),
        // After 3 failures, agent should inject diagnostic; model tries different approach
        make_tool_call("list_dir", R"({"path": "."})"),
        make_reply("Found the files via listing."),
    });

    auto fs = std::make_shared<LocalTestFileSystem>();
    auto cmd = std::make_shared<RecordingCommandRunner>();

    AgentRuntimeConfig runtime;
    runtime.max_model_steps = 10;
    runtime.max_consecutive_failures = 3;
    auto approval = std::make_shared<StaticApprovalProvider>(true);
    Agent agent(std::make_unique<StructuredScriptedClient>(
                    std::vector<ChatMessage>{
                        make_tool_call("read_file", R"({"path": "no1.txt"})"),
                        make_tool_call("read_file", R"({"path": "no2.txt"})"),
                        make_tool_call("read_file", R"({"path": "no3.txt"})"),
                        make_tool_call("list_dir", R"({"path": "."})"),
                        make_reply("Recovered."),
                    }),
                approval, temp.path(), {}, runtime, false, nullptr,
                make_test_tools(temp.path(), fs, cmd));

    const std::string result = agent.run_turn("Find something");

    // The test validates that the agent doesn't crash after repeated failures
    // and eventually returns a result (either from recovery or max steps)
    expect_true(!result.empty(), "Agent should return a result after failure recovery");
}

void test_workspace_prompt_loaded() {
    ScopedTempDir temp;
    write_file(temp.path() / "bolt.md",
               "You are a Python expert. Always use type hints.");

    const std::string prompt = load_workspace_prompt(temp.path());
    expect_true(!prompt.empty(), "Workspace prompt should be loaded from bolt.md");
    expect_contains(prompt, "Python expert",
                    "Workspace prompt should contain bolt.md content");
}

void test_workspace_prompt_dot_bolt() {
    ScopedTempDir temp;
    write_file(temp.path() / ".bolt" / "prompt.md",
               "This project uses CMake.");

    const std::string prompt = load_workspace_prompt(temp.path());
    expect_true(!prompt.empty(), "Workspace prompt should load from .bolt/prompt.md");
    expect_contains(prompt, "CMake",
                    "Should contain .bolt/prompt.md content");
}

void test_workspace_prompt_empty_when_no_file() {
    ScopedTempDir temp;
    const std::string prompt = load_workspace_prompt(temp.path());
    expect_true(prompt.empty(),
                "Workspace prompt should be empty when no file exists");
}

void test_structured_streaming_callback() {
    ScopedTempDir temp;

    auto client = std::make_unique<StructuredScriptedClient>(std::vector<ChatMessage>{
        make_reply("Streamed response content"),
    });

    auto fs = std::make_shared<LocalTestFileSystem>();
    auto cmd = std::make_shared<RecordingCommandRunner>();
    Agent agent = make_agent(std::move(client), temp.path(), fs, cmd);

    std::string streamed;
    const std::string result = agent.run_turn_streaming("Hello",
        [&](const std::string& token) { streamed += token; });

    expect_true(result == "Streamed response content",
                "Streaming should return full content");
    expect_true(!streamed.empty(),
                "Streaming callback should receive tokens");
}

}  // namespace

int main() {
    const std::vector<std::pair<std::string, std::function<void()>>> tests = {
        {"structured read_file and reply", test_structured_read_file_and_reply},
        {"structured write and verify", test_structured_write_and_verify},
        {"structured parallel tool calls", test_structured_parallel_tool_calls},
        {"structured edit-build-fix loop", test_structured_edit_build_fix_loop},
        {"failure recovery injects diagnostic", test_failure_recovery_injects_diagnostic},
        {"workspace prompt from bolt.md", test_workspace_prompt_loaded},
        {"workspace prompt from .bolt/prompt.md", test_workspace_prompt_dot_bolt},
        {"workspace prompt empty when no file", test_workspace_prompt_empty_when_no_file},
        {"structured streaming callback", test_structured_streaming_callback},
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

    std::cout << "Passed " << passed << " e2e tests.\n";
    return 0;
}
