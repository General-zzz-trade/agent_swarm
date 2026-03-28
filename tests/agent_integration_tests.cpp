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
#include "../src/app/app_config.h"
#include "../src/app/approval_provider_factory.h"

namespace {

class ScopedTempDir {
public:
    ScopedTempDir() {
        const auto base = std::filesystem::temp_directory_path();
        path_ = base / std::filesystem::path("mini_nn_cpp_integration_" +
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
        return {false, {}, "Not implemented in integration tests"};
    }

    bool create_directories(const std::filesystem::path& path,
                            std::string& error) const override {
        std::error_code ec;
        (void)std::filesystem::create_directories(path, ec);
        if (ec) {
            error = ec.message();
            return false;
        }
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
        return {false, {}, false, "Not implemented in integration tests"};
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

class ScriptedModelClient : public IModelClient {
public:
    explicit ScriptedModelClient(std::vector<std::string> responses,
                                 std::string model_name = "integration-model")
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

ToolRegistry make_test_tool_registry(
    const std::filesystem::path& workspace_root,
    const std::shared_ptr<IFileSystem>& file_system,
    const std::shared_ptr<ICommandRunner>& command_runner) {
    return create_default_tool_registry(workspace_root, file_system, command_runner, nullptr, {},
                                        nullptr, nullptr, nullptr);
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

void write_text_file(const std::filesystem::path& path, const std::string& content) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        throw std::runtime_error("Failed to write test config: " + path.string());
    }
    output << content;
}

void expect_prompt_mode_runs_command_round_trip() {
    ScopedTempDir temp_dir;
    write_text_file(temp_dir.path() / "mini_nn_cpp.conf",
                    "approval.mode=prompt\n"
                    "agent.max_model_steps=3\n");

    const AppConfig config = load_app_config(temp_dir.path());
    const auto file_system = std::make_shared<LocalTestFileSystem>();
    const auto command_runner = std::make_shared<RecordingCommandRunner>();
    command_runner->result = {true, false, 0, "qwen3:8b", ""};

    std::istringstream input("y\n");
    std::ostringstream output;
    auto approval_provider = create_approval_provider(config.approval, input, output);

    auto scripted_client = std::make_unique<ScriptedModelClient>(std::vector<std::string>{
        R"({"action":"tool","tool":"run_command","args":"ollama list","reason":"Need local state","risk":"medium","requires_confirmation":"true"})",
        R"({"action":"reply","content":"Prompt mode completed","reason":"Tool result is enough","risk":"low","requires_confirmation":"false"})",
    });
    ScriptedModelClient* client_ptr = scripted_client.get();

    Agent agent(std::move(scripted_client), approval_provider, temp_dir.path(), config.policy,
                config.agent_runtime, false, nullptr,
                make_test_tool_registry(temp_dir.path(), file_system, command_runner));
    const std::string result = agent.run_turn("Run ollama list.");

    expect_equal(result, "Prompt mode completed",
                 "Prompt approval mode should allow the full tool round trip");
    expect_equal(command_runner->last_command, "ollama list",
                 "Approved prompt mode should execute the command");
    expect_true(output.str().find("[approval]") != std::string::npos,
                "Prompt mode should render the approval prompt");
    expect_true(client_ptr->prompts().size() == 2,
                "Prompt mode round trip should need two model responses");
}

void expect_auto_approve_mode_runs_without_prompt() {
    ScopedTempDir temp_dir;
    write_text_file(temp_dir.path() / "mini_nn_cpp.conf",
                    "approval.mode=auto-approve\n"
                    "agent.max_model_steps=3\n");

    const AppConfig config = load_app_config(temp_dir.path());
    const auto file_system = std::make_shared<LocalTestFileSystem>();
    const auto command_runner = std::make_shared<RecordingCommandRunner>();
    command_runner->result = {true, false, 0, "qwen3:8b", ""};

    std::istringstream input("");
    std::ostringstream output;
    auto approval_provider = create_approval_provider(config.approval, input, output);

    auto scripted_client = std::make_unique<ScriptedModelClient>(std::vector<std::string>{
        R"({"action":"tool","tool":"run_command","args":"ollama list","reason":"Need local state","risk":"medium","requires_confirmation":"true"})",
        R"({"action":"reply","content":"Auto approve completed","reason":"Tool result is enough","risk":"low","requires_confirmation":"false"})",
    });

    Agent agent(std::move(scripted_client), approval_provider, temp_dir.path(), config.policy,
                config.agent_runtime, false, nullptr,
                make_test_tool_registry(temp_dir.path(), file_system, command_runner));
    const std::string result = agent.run_turn("Run ollama list.");

    expect_equal(result, "Auto approve completed",
                 "Auto-approve mode should execute approval-required tools");
    expect_equal(command_runner->last_command, "ollama list",
                 "Auto-approve mode should execute the command");
    expect_true(output.str().empty(),
                "Auto-approve mode should not emit an interactive prompt");
}

void expect_auto_deny_mode_blocks_without_prompt() {
    ScopedTempDir temp_dir;
    write_text_file(temp_dir.path() / "mini_nn_cpp.conf",
                    "approval.mode=auto-deny\n"
                    "agent.max_model_steps=3\n");

    const AppConfig config = load_app_config(temp_dir.path());
    const auto file_system = std::make_shared<LocalTestFileSystem>();
    const auto command_runner = std::make_shared<RecordingCommandRunner>();

    std::istringstream input("");
    std::ostringstream output;
    auto approval_provider = create_approval_provider(config.approval, input, output);

    auto scripted_client = std::make_unique<ScriptedModelClient>(std::vector<std::string>{
        R"({"action":"tool","tool":"run_command","args":"ollama list","reason":"Need local state","risk":"medium","requires_confirmation":"true"})",
    });

    Agent agent(std::move(scripted_client), approval_provider, temp_dir.path(), config.policy,
                config.agent_runtime, false, nullptr,
                make_test_tool_registry(temp_dir.path(), file_system, command_runner));
    const std::string result = agent.run_turn("Run ollama list.");

    expect_true(result.find("Approval denied for run_command") != std::string::npos,
                "Auto-deny mode should block the approval-required tool");
    expect_true(command_runner->last_command.empty(),
                "Auto-deny mode should not execute the command");
    expect_true(output.str().empty(),
                "Auto-deny mode should not emit an interactive prompt");
}

}  // namespace

int main() {
    const std::vector<std::pair<std::string, std::function<void()>>> tests = {
        {"prompt mode runs command round trip", expect_prompt_mode_runs_command_round_trip},
        {"auto approve mode runs without prompt", expect_auto_approve_mode_runs_without_prompt},
        {"auto deny mode blocks without prompt", expect_auto_deny_mode_blocks_without_prompt},
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

    std::cout << "Passed " << passed << " agent integration tests.\n";
    return 0;
}
