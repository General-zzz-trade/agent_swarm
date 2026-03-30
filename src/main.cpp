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
#include "app/setup_wizard.h"
#include "app/static_approval_provider.h"
#include "app/benchmark_runner.h"
#include "core/mcp/mcp_server.h"
#include "app/program_cli.h"
#include "app/telegram_gateway.h"
#include "app/discord_gateway.h"
#include "app/web_approval_provider.h"
#include "app/api_server.h"
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

    // Quick pre-parse to check if there's a prompt (for wizard decision)
    const auto args = collect_cli_args(argc, argv, 2);

    // Run setup wizard on first interactive launch
    if (!is_setup_complete()) {
        bool is_interactive = true;
        for (size_t i = 0; i < args.size(); ++i) {
            const auto& a = args[i];
            if (a == "--debug" || a == "--no-debug" || a == "--resume") continue;
            if (a == "--model" && i + 1 < args.size()) { ++i; continue; }
            if (a.rfind("--", 0) != 0 && a.find(':') == std::string::npos) {
                is_interactive = false;  // has a prompt
                break;
            }
        }
        if (is_interactive) {
            run_setup_wizard();
        }
    }

    const AppConfig config = load_app_config(workspace_root);
    const AgentCliOptions options =
        resolve_agent_cli_options(args, config);
    std::unique_ptr<Agent> agent =
        create_platform_agent(workspace_root, config, options, std::cin, std::cout);

    if (!options.prompt.empty()) {
        return run_agent_single_turn(*agent, options.prompt, std::cout);
    }

    return run_agent_interactive_loop(*agent, std::cin, std::cout, workspace_root, options.resume);
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
        services.process_manager, services.ui_automation, services.window_controller,
        services.http_transport);

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

int run_telegram(int argc, char* argv[]) {
    (void)argc;
    (void)argv;
    const std::filesystem::path workspace_root = std::filesystem::current_path();
    const AppConfig config = load_app_config(workspace_root);

    // Get bot token from env
    const char* token_env = std::getenv("TELEGRAM_BOT_TOKEN");
    if (!token_env || std::string(token_env).empty()) {
        std::cerr << "Error: TELEGRAM_BOT_TOKEN environment variable is required.\n";
        std::cerr << "Get a token from @BotFather on Telegram.\n";
        return 1;
    }

    AgentCliOptions agent_options;
    agent_options.model = config.default_model;

    AgentServices services =
        create_platform_agent_services(config, agent_options, std::cin, std::cout);
    // Use static approval (auto-approve) for Telegram since there is no
    // interactive terminal to prompt.
    auto static_approval = std::make_shared<StaticApprovalProvider>(true);
    services.approval_provider = static_approval;

    // Keep a reference to the transport before moving services
    auto transport = services.http_transport;

    std::unique_ptr<Agent> agent =
        create_agent(workspace_root, config, agent_options, std::move(services));

    TelegramGateway gateway(token_env, *agent, transport, workspace_root);
    return gateway.run();
}

int run_discord(int argc, char* argv[]) {
    (void)argc;
    (void)argv;
    const std::filesystem::path workspace_root = std::filesystem::current_path();
    const AppConfig config = load_app_config(workspace_root);

    // Get bot token from env
    const char* token_env = std::getenv("DISCORD_BOT_TOKEN");
    if (!token_env || std::string(token_env).empty()) {
        std::cerr << "Error: DISCORD_BOT_TOKEN environment variable is required.\n";
        std::cerr << "Get a token from the Discord Developer Portal.\n";
        return 1;
    }

    // Get channel ID from env
    const char* channel_env = std::getenv("DISCORD_CHANNEL_ID");
    if (!channel_env || std::string(channel_env).empty()) {
        std::cerr << "Error: DISCORD_CHANNEL_ID environment variable is required.\n";
        std::cerr << "Set it to the channel ID the bot should listen to.\n";
        return 1;
    }

    AgentCliOptions agent_options;
    agent_options.model = config.default_model;

    AgentServices services =
        create_platform_agent_services(config, agent_options, std::cin, std::cout);
    // Use static approval (auto-approve) for Discord since there is no
    // interactive terminal to prompt.
    auto static_approval = std::make_shared<StaticApprovalProvider>(true);
    services.approval_provider = static_approval;

    // Keep a reference to the transport before moving services
    auto transport = services.http_transport;

    std::unique_ptr<Agent> agent =
        create_agent(workspace_root, config, agent_options, std::move(services));

    DiscordGateway gateway(token_env, channel_env, *agent, transport, workspace_root);
    return gateway.run();
}

int run_api_server(int argc, char* argv[]) {
    const std::filesystem::path workspace_root = std::filesystem::current_path();
    const AppConfig config = load_app_config(workspace_root);

    // Parse --port option (default 9090)
    unsigned short port = 9090;
    auto args = collect_cli_args(argc, argv, 2);
    for (std::size_t i = 0; i < args.size(); ++i) {
        if (args[i] == "--port" && i + 1 < args.size()) {
            try {
                int p = std::stoi(args[i + 1]);
                if (p > 0 && p <= 65535) port = static_cast<unsigned short>(p);
            } catch (...) {}
            ++i;
        }
    }

    AgentCliOptions agent_options;
    agent_options.model = config.default_model;

    AgentServices services =
        create_platform_agent_services(config, agent_options, std::cin, std::cout);
    // Use static approval (auto-approve) for API server since there is no
    // interactive terminal to prompt.
    auto static_approval = std::make_shared<StaticApprovalProvider>(true);
    services.approval_provider = static_approval;

    std::unique_ptr<Agent> agent =
        create_agent(workspace_root, config, agent_options, std::move(services));

    ApiServer server(workspace_root, *agent, port);
    return server.run(std::cout);
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
            case TopLevelCommandType::telegram:
                return run_telegram(argc, argv);
            case TopLevelCommandType::discord:
                return run_discord(argc, argv);
            case TopLevelCommandType::bench:
                return run_benchmark(argc, argv);
            case TopLevelCommandType::mcp_server:
                return run_mcp_server();
            case TopLevelCommandType::api_server:
                return run_api_server(argc, argv);
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
