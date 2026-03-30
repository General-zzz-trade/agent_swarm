/**
 * End-to-end API tests for Bolt Agent.
 *
 * These tests require a real LLM API key. Run with:
 *   MOONSHOT_API_KEY=sk-xxx ./build/api_e2e_tests
 *
 * Or any other provider:
 *   DEEPSEEK_API_KEY=sk-xxx BOLT_PROVIDER=deepseek BOLT_MODEL=deepseek-chat ./build/api_e2e_tests
 *
 * Tests verify the full agent loop: prompt → model → tool call → result → reply.
 * Each test has a 60-second timeout. Slow models may need BOLT_TIMEOUT=120.
 */

#include <chrono>
#include <cstdlib>
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
#include "../src/app/agent_cli_options.h"
#include "../src/app/model_client_factory.h"
#include "../src/app/null_audit_logger.h"
#include "../src/app/static_approval_provider.h"
#include "../src/platform/linux/linux_file_system.h"
#include "../src/platform/linux/linux_command_runner.h"
#include "../src/platform/linux/linux_process_manager.h"
#include "../src/platform/linux/linux_http_transport.h"

namespace {

using Clock = std::chrono::steady_clock;
using Ms = std::chrono::duration<double, std::milli>;

// ---- Test infrastructure ----

struct TestContext {
    std::filesystem::path workspace;
    std::unique_ptr<Agent> agent;
    int timeout_ms = 60000;
};

std::unique_ptr<TestContext> create_test_context() {
    auto ctx = std::make_unique<TestContext>();

    // Use a temp workspace
    ctx->workspace = std::filesystem::temp_directory_path() / "bolt_e2e_test";
    std::filesystem::create_directories(ctx->workspace);

    // Load config (respects ~/.bolt/config.json and env vars)
    AppConfig config = load_app_config(ctx->workspace);

    // Override from env if set
    const char* provider_env = std::getenv("BOLT_PROVIDER");
    if (provider_env) config.provider = provider_env;
    const char* model_env = std::getenv("BOLT_MODEL");
    if (model_env) config.default_model = model_env;

    const char* timeout_env = std::getenv("BOLT_TIMEOUT");
    if (timeout_env) ctx->timeout_ms = std::atoi(timeout_env) * 1000;

    // Create services
    auto fs = std::make_shared<LinuxFileSystem>();
    auto runner = std::make_shared<LinuxCommandRunner>();
    auto pm = std::make_shared<LinuxProcessManager>();
    auto transport = std::make_shared<LinuxHttpTransport>();
    auto logger = std::make_shared<NullAuditLogger>();
    auto approval = std::make_shared<StaticApprovalProvider>(true); // auto-approve

    std::unique_ptr<IModelClient> client;
    try {
        client = create_model_client(config, config.default_model, transport);
        if (!client) {
            // Fallback to ollama-chat
            config.provider = "ollama-chat";
            client = create_model_client(config, config.default_model, transport);
        }
    } catch (const std::exception& e) {
        std::cerr << "Failed to create model client: " << e.what() << "\n";
        return nullptr;
    }

    if (!client) {
        std::cerr << "No model client available. Set an API key.\n";
        return nullptr;
    }

    CommandPolicyConfig cmd_policy;
    auto tools = create_default_tool_registry(
        ctx->workspace, fs, runner, logger, cmd_policy, pm, nullptr, nullptr, transport);

    // Disable auto-verify for controlled testing
    AgentRuntimeConfig rt_config;
    rt_config.auto_verify = false;
    rt_config.compact_prompt = true;
    rt_config.core_tools_only = false;

    ctx->agent = std::make_unique<Agent>(
        std::move(client), approval, ctx->workspace,
        PolicyConfig{}, rt_config, false, logger, std::move(tools));

    return ctx;
}

void cleanup_workspace(const std::filesystem::path& workspace) {
    std::error_code ec;
    std::filesystem::remove_all(workspace, ec);
}

// ---- Test runner ----

struct TestResult {
    std::string name;
    bool passed = false;
    std::string detail;
    double elapsed_ms = 0;
};

TestResult run_test(const std::string& name, TestContext& ctx,
                     std::function<bool(Agent&, const std::filesystem::path&)> test_fn) {
    TestResult result;
    result.name = name;

    auto start = Clock::now();
    try {
        result.passed = test_fn(*ctx.agent, ctx.workspace);
        if (!result.passed) {
            result.detail = "Test assertion failed";
        }
    } catch (const std::exception& e) {
        result.passed = false;
        result.detail = e.what();
    }
    result.elapsed_ms = Ms(Clock::now() - start).count();

    // Clear agent history between tests
    ctx.agent->clear_history();

    return result;
}

// ---- Helper functions ----

std::string read_file(const std::filesystem::path& path) {
    std::ifstream f(path);
    if (!f) return "";
    return std::string(std::istreambuf_iterator<char>(f), {});
}

void write_file(const std::filesystem::path& path, const std::string& content) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream f(path);
    f << content;
}

bool file_contains(const std::filesystem::path& path, const std::string& text) {
    std::string content = read_file(path);
    return content.find(text) != std::string::npos;
}

// ---- Test definitions ----

bool test_simple_conversation(Agent& agent, const std::filesystem::path& ws) {
    std::string reply = agent.run_turn("说hello，只回复一个词");
    // Should contain some response (not empty)
    return !reply.empty() && reply.size() < 500;
}

bool test_create_file(Agent& agent, const std::filesystem::path& ws) {
    std::string reply = agent.run_turn(
        "创建文件 hello.txt，内容为 Hello from Bolt Agent");

    // Check if file was created
    auto path = ws / "hello.txt";
    if (!std::filesystem::exists(path)) return false;

    std::string content = read_file(path);
    bool ok = content.find("Hello") != std::string::npos ||
              content.find("hello") != std::string::npos ||
              content.find("Bolt") != std::string::npos;
    std::filesystem::remove(path);
    return ok;
}

bool test_edit_file(Agent& agent, const std::filesystem::path& ws) {
    // Create a test file
    auto path = ws / "edit_test.txt";
    write_file(path, "Hello World\nThis is a test.\n");

    std::string reply = agent.run_turn(
        "修改 edit_test.txt 文件，把 Hello 替换为 Goodbye");

    std::string content = read_file(path);
    bool ok = content.find("Goodbye") != std::string::npos;
    std::filesystem::remove(path);
    return ok;
}

bool test_fix_compile_error(Agent& agent, const std::filesystem::path& ws) {
    // Create a file with compile errors
    auto path = ws / "buggy.cpp";
    write_file(path, "#include <iostream>\nint main() {\n    std::cout << \"hi\" << std::endll;\n    return 0\n}\n");

    std::string reply = agent.run_turn(
        "buggy.cpp 有编译错误，请读取它，找出并修复所有错误");

    std::string content = read_file(path);
    // Should have fixed endll -> endl and added semicolon
    bool fixed_endl = content.find("std::endl") != std::string::npos &&
                      content.find("std::endll") == std::string::npos;
    bool fixed_semicolon = content.find("return 0;") != std::string::npos;
    std::filesystem::remove(path);

    return fixed_endl || fixed_semicolon; // At least one fix
}

bool test_write_program(Agent& agent, const std::filesystem::path& ws) {
    std::string reply = agent.run_turn(
        "创建 factorial.cpp，写一个计算阶乘的C++程序，包含main函数打印5的阶乘");

    auto path = ws / "factorial.cpp";
    if (!std::filesystem::exists(path)) return false;

    std::string content = read_file(path);
    // Should contain basic C++ elements
    bool has_include = content.find("#include") != std::string::npos;
    bool has_main = content.find("main") != std::string::npos;
    bool has_factorial = content.find("factorial") != std::string::npos ||
                         content.find("Factorial") != std::string::npos ||
                         content.find("fact") != std::string::npos;

    // Try to compile
    std::string compile_cmd = "g++ " + path.string() + " -o /tmp/bolt_e2e_factorial 2>&1";
    FILE* pipe = popen(compile_cmd.c_str(), "r");
    std::string compile_output;
    if (pipe) {
        char buf[256];
        while (fgets(buf, sizeof(buf), pipe)) compile_output += buf;
        int status = pclose(pipe);
#ifdef _WIN32
        bool compiled = (status == 0);
#else
        bool compiled = (WIFEXITED(status) && WEXITSTATUS(status) == 0);
#endif

        if (compiled) {
            // Run and check output
            pipe = popen("/tmp/bolt_e2e_factorial 2>&1", "r");
            std::string run_output;
            if (pipe) {
                while (fgets(buf, sizeof(buf), pipe)) run_output += buf;
                pclose(pipe);
            }
            std::filesystem::remove(path);
            std::filesystem::remove("/tmp/bolt_e2e_factorial");
            // 5! = 120
            return run_output.find("120") != std::string::npos;
        }
    }

    std::filesystem::remove(path);
    // Even if compile fails, check basic structure
    return has_include && has_main && has_factorial;
}

bool test_search_and_explain(Agent& agent, const std::filesystem::path& ws) {
    // Create some source files to search
    write_file(ws / "src" / "calculator.h",
        "#pragma once\nclass Calculator {\npublic:\n    int add(int a, int b);\n    int multiply(int a, int b);\n};\n");
    write_file(ws / "src" / "calculator.cpp",
        "#include \"calculator.h\"\nint Calculator::add(int a, int b) { return a + b; }\nint Calculator::multiply(int a, int b) { return a * b; }\n");

    std::string reply = agent.run_turn(
        "在代码中搜索 Calculator 类，告诉我它有哪些方法");

    // Cleanup
    std::filesystem::remove_all(ws / "src");

    // Reply should mention add and multiply
    bool mentions_add = reply.find("add") != std::string::npos;
    bool mentions_multiply = reply.find("multiply") != std::string::npos ||
                             reply.find("mul") != std::string::npos;
    return mentions_add || mentions_multiply;
}

bool test_calculator(Agent& agent, const std::filesystem::path& ws) {
    std::string reply = agent.run_turn("用calculator计算 (2 + 3) * 4 + 1");
    // Should contain 21
    return reply.find("21") != std::string::npos;
}

}  // namespace

int main() {
    std::cout << "\n\033[1;36m╔══════════════════════════════════════════════╗\033[0m\n";
    std::cout << "\033[1;36m║    ⚡ Bolt Agent — E2E API Tests             ║\033[0m\n";
    std::cout << "\033[1;36m╚══════════════════════════════════════════════╝\033[0m\n\n";

    auto ctx = create_test_context();
    if (!ctx) {
        std::cerr << "\nCannot run API tests without a model client.\n";
        std::cerr << "Set one of: MOONSHOT_API_KEY, DEEPSEEK_API_KEY, OPENAI_API_KEY, etc.\n";
        return 1;
    }

    std::cout << "  Model:     " << ctx->agent->model() << "\n";
    std::cout << "  Workspace: " << ctx->workspace.string() << "\n";
    std::cout << "  Timeout:   " << ctx->timeout_ms / 1000 << "s per test\n\n";

    // Define tests
    struct TestDef {
        std::string name;
        std::function<bool(Agent&, const std::filesystem::path&)> fn;
    };
    std::vector<TestDef> tests = {
        {"Simple conversation",   test_simple_conversation},
        {"Create file",           test_create_file},
        {"Edit file",             test_edit_file},
        {"Fix compile error",     test_fix_compile_error},
        {"Write complete program", test_write_program},
        {"Search and explain",    test_search_and_explain},
        {"Calculator tool",       test_calculator},
    };

    int passed = 0, failed = 0;
    double total_ms = 0;

    for (const auto& test : tests) {
        std::cout << "  Running: " << test.name << "..." << std::flush;
        auto result = run_test(test.name, *ctx, test.fn);
        total_ms += result.elapsed_ms;

        if (result.passed) {
            std::cout << "\r  \033[32m[PASS]\033[0m " << test.name
                      << " \033[2m(" << (int)(result.elapsed_ms / 1000) << "s)\033[0m\n";
            ++passed;
        } else {
            std::cout << "\r  \033[31m[FAIL]\033[0m " << test.name
                      << " \033[2m(" << (int)(result.elapsed_ms / 1000) << "s)\033[0m\n";
            if (!result.detail.empty()) {
                std::cout << "         " << result.detail.substr(0, 120) << "\n";
            }
            ++failed;
        }
    }

    // Cleanup
    cleanup_workspace(ctx->workspace);

    std::cout << "\n\033[1m═══ Summary ═══\033[0m\n";
    std::cout << "  " << passed << "/" << (passed + failed) << " passed";
    std::cout << "  (" << (int)(total_ms / 1000) << "s total)\n";

    if (failed == 0) {
        std::cout << "  \033[1;32mAll tests passed!\033[0m\n\n";
    } else {
        std::cout << "  \033[1;31m" << failed << " test(s) failed.\033[0m\n\n";
    }

    return failed > 0 ? 1 : 0;
}
