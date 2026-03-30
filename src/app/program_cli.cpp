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
    if (command == "wechat") {
        return {TopLevelCommandType::wechat, command};
    }
    if (command == "slack") {
        return {TopLevelCommandType::slack, command};
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
    if (command == "--version" || command == "-v" || command == "version") {
        return {TopLevelCommandType::version, command};
    }
    if (command == "doctor") {
        return {TopLevelCommandType::doctor, command};
    }
    if (command == "init") {
        return {TopLevelCommandType::init_workspace, command};
    }
    // Unknown command — treat as agent prompt (e.g. "bolt 'Read main.cpp'")
    return {TopLevelCommandType::invalid, command};
}

std::string build_usage_text(const std::string& program_name) {
    std::ostringstream output;
    output << "\n\033[1;36m⚡ Bolt\033[0m — AI Coding Agent (v0.5.0)\n\n";

    output << "\033[1mUsage:\033[0m\n";
    output << "  " << program_name << "                              Interactive agent (default)\n";
    output << "  " << program_name << " agent [options] [prompt]     Ask a question or give a task\n";
    output << "  " << program_name << " api-server [--port 9090]     REST API + Web UI server\n";
    output << "  " << program_name << " web-chat [--port 8080]       Legacy chat UI\n";
    output << "  " << program_name << " mcp-server                   MCP protocol (Claude Code/Cursor)\n";
    output << "  " << program_name << " telegram                     Telegram bot\n";
    output << "  " << program_name << " discord                      Discord bot\n";
    output << "  " << program_name << " wechat                       WeChat bot\n";
    output << "  " << program_name << " slack                        Slack bot\n";
    output << "  " << program_name << " bench [--rounds 5]           Performance benchmark\n";
    output << "  " << program_name << " init                         Create bolt.md project config\n";
    output << "  " << program_name << " doctor                       Check environment & dependencies\n";

    output << "\n\033[1mAgent options:\033[0m\n";
    output << "  --model <name>       Set LLM model (e.g. moonshot-v1-128k, gpt-4o)\n";
    output << "  --debug              Enable debug logging\n";
    output << "  --resume             Resume last session\n";
    output << "  -p, --print          Non-interactive mode (pipe stdin)\n";

    output << "\n\033[1mGlobal options:\033[0m\n";
    output << "  -v, --version        Show version\n";
    output << "  -h, --help           Show this help\n";

    output << "\n\033[1mExamples:\033[0m\n";
    output << "  " << program_name << " agent \"Read src/main.cpp and explain it\"\n";
    output << "  " << program_name << " agent -p \"Fix the bug\" < error.log\n";
    output << "  " << program_name << " api-server --port 19090\n";
    output << "  " << program_name << " init\n";
    output << "  MOONSHOT_API_KEY=sk-xxx " << program_name << "\n";

    output << "\n\033[1mConfig:\033[0m bolt.conf | ~/.bolt/config.json | BOLT_PROVIDER, OPENAI_API_KEY, etc.\n";
    output << "\033[2mDocs: https://github.com/General-zzz-trade/Bolt\033[0m\n";
    return output.str();
}
