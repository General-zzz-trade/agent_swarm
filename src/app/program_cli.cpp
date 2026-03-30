#include "program_cli.h"

#include <sstream>

TopLevelCommand resolve_top_level_command(const std::vector<std::string>& args) {
    if (args.empty()) {
        // No args → default to interactive agent mode (not usage text)
        return {TopLevelCommandType::agent, "agent"};
    }

    const std::string& command = args.front();
    if (command == "train-demo") {
        return {TopLevelCommandType::train_demo, command};
    }
    if (command == "agent") {
        return {TopLevelCommandType::agent, command};
    }
    if (command == "web-chat") {
        return {TopLevelCommandType::web_chat, command};
    }
    if (command == "telegram") {
        return {TopLevelCommandType::telegram, command};
    }
    if (command == "discord") {
        return {TopLevelCommandType::discord, command};
    }
    if (command == "bench") {
        return {TopLevelCommandType::bench, command};
    }
    if (command == "mcp-server" || command == "mcp") {
        return {TopLevelCommandType::mcp_server, command};
    }
    if (command == "api-server" || command == "api") {
        return {TopLevelCommandType::api_server, command};
    }
    if (command == "--help" || command == "-h" || command == "help") {
        return {TopLevelCommandType::usage, command};
    }
    // Unknown command — treat as agent prompt (e.g. "bolt 'Read main.cpp'")
    return {TopLevelCommandType::invalid, command};
}

std::string build_usage_text(const std::string& program_name) {
    std::ostringstream output;
    output << "\n\033[1m\033[36m⚡ Bolt\033[0m — AI Coding Agent\n\n";
    output << "Usage:\n";
    output << "  " << program_name << "                          Interactive agent (default)\n";
    output << "  " << program_name << " agent [prompt]            Ask a question or give a task\n";
    output << "  " << program_name << " web-chat [--port 8080]    Browser-based chat UI\n";
    output << "  " << program_name << " telegram                  Telegram bot gateway (TELEGRAM_BOT_TOKEN)\n";
    output << "  " << program_name << " discord                   Discord bot gateway (DISCORD_BOT_TOKEN)\n";
    output << "  " << program_name << " bench [--rounds 5]        Performance benchmark\n";
    output << "  " << program_name << " mcp-server               MCP protocol server (stdin/stdout)\n";
    output << "  " << program_name << " api-server [--port 9090] REST API server\n";
    output << "  " << program_name << " --help                    Show this help\n\n";
    output << "Examples:\n";
    output << "  " << program_name << " agent \"Read src/main.cpp and explain it\"\n";
    output << "  " << program_name << " agent \"Search for TODO comments\"\n";
    output << "  " << program_name << " agent \"Add error handling to the login function\"\n";
    output << "  " << program_name << " web-chat --port 8080\n\n";
    output << "Config: bolt.conf | Env: BOLT_PROVIDER, OPENAI_API_KEY, etc.\n";
    return output.str();
}
