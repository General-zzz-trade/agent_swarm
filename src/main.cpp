#include <filesystem>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

#ifdef _WIN32
#include <windows.h>
#endif

#include "app/agent_cli_options.h"
#include "app/agent_factory.h"
#include "app/app_config.h"
#include "app/agent_runner.h"
#include "app/benchmark_runner.h"
#include "core/mcp/mcp_server.h"
#include "app/program_cli.h"
#include "app/web_approval_provider.h"
#include "app/web_chat_cli_options.h"
#include "app/web_chat_server.h"
#include "agent/agent.h"
#include "agent/tool_set_factory.h"
#include "demo/training_demo.h"
#include "platform/platform_agent_factory.h"

namespace {

void configure_console() {
#ifdef _WIN32
    SetConsoleCP(CP_UTF8);
    SetConsoleOutputCP(CP_UTF8);
#endif
}

void print_usage(const char* program_name) {
    std::cout << build_usage_text(program_name);
}

int run_agent(int argc, char* argv[]) {
    const std::filesystem::path workspace_root = std::filesystem::current_path();
    const AppConfig config = load_app_config(workspace_root);
    const AgentCliOptions options =
        resolve_agent_cli_options(collect_cli_args(argc, argv, 2), config);
    std::unique_ptr<Agent> agent =
        create_platform_agent(workspace_root, config, options, std::cin, std::cout);

    if (!options.prompt.empty()) {
        return run_agent_single_turn(*agent, options.prompt, std::cout);
    }

    return run_agent_interactive_loop(*agent, std::cin, std::cout, workspace_root);
}

int run_web_chat(int argc, char* argv[]) {
    const std::filesystem::path workspace_root = std::filesystem::current_path();
    const AppConfig config = load_app_config(workspace_root);
    const WebChatCliOptions options =
        resolve_web_chat_cli_options(collect_cli_args(argc, argv, 2), config);

    AgentCliOptions agent_options;
    agent_options.debug = options.debug;
    agent_options.model = options.model;
    agent_options.prompt.clear();

    AgentServices services =
        create_platform_agent_services(config, agent_options, std::cin, std::cout);
    const std::shared_ptr<WebApprovalProvider> web_approval_provider =
        std::make_shared<WebApprovalProvider>();
    services.approval_provider = web_approval_provider;

    std::unique_ptr<Agent> agent =
        create_agent(workspace_root, config, agent_options, std::move(services));
    WebChatServer server(workspace_root, *agent, web_approval_provider, options.port);
    return server.run(std::cout);
}

int run_mcp_server() {
    const std::filesystem::path workspace_root = std::filesystem::current_path();
    const AppConfig config = load_app_config(workspace_root);

    AgentCliOptions options;
    AgentServices services =
        create_platform_agent_services(config, options, std::cin, std::cout);

    // Build tool registry for MCP
    auto tools = create_default_tool_registry(
        workspace_root, services.file_system, services.command_runner,
        services.audit_logger, config.command_policy,
        services.process_manager, services.ui_automation, services.window_controller);

    McpServer server;
    for (const Tool* tool : tools.list()) {
        const std::string tool_name = tool->name();
        server.register_tool({
            tool_name, tool->description(), tool->schema(),
            [&tools, tool_name](const std::string& args) -> std::string {
                const Tool* t = tools.find(tool_name);
                if (!t) return "Tool not found: " + tool_name;
                ToolResult r = t->run(args);
                return r.content;
            }
        });
    }

    return server.run(std::cin, std::cout);
}

int run_benchmark(int argc, char* argv[]) {
    const std::filesystem::path workspace_root = std::filesystem::current_path();
    const AppConfig config = load_app_config(workspace_root);
    const BenchmarkConfig bench_config =
        resolve_bench_config(collect_cli_args(argc, argv, 2));
    run_benchmarks(config, workspace_root, bench_config, std::cout);
    return 0;
}

}  // namespace

int main(int argc, char* argv[]) {
    configure_console();

    try {
        const TopLevelCommand command =
            resolve_top_level_command(collect_cli_args(argc, argv, 1));
        switch (command.type) {
            case TopLevelCommandType::usage:
                print_usage(argv[0]);
                return 0;
            case TopLevelCommandType::train_demo:
                run_training_demo();
                return 0;
            case TopLevelCommandType::agent:
                return run_agent(argc, argv);
            case TopLevelCommandType::web_chat:
                return run_web_chat(argc, argv);
            case TopLevelCommandType::bench:
                return run_benchmark(argc, argv);
            case TopLevelCommandType::mcp_server:
                return run_mcp_server();
            case TopLevelCommandType::invalid:
                print_usage(argv[0]);
                return 1;
        }
        throw std::runtime_error("Unsupported top-level command state");
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
}
