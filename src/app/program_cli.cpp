#include "program_cli.h"

#include <sstream>

TopLevelCommand resolve_top_level_command(const std::vector<std::string>& args) {
    if (args.empty()) {
        return {TopLevelCommandType::usage, ""};
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
    return {TopLevelCommandType::invalid, command};
}

std::string build_usage_text(const std::string& program_name) {
    std::ostringstream output;
    output << "Usage:\n";
    output << "  " << program_name << " train-demo\n";
    output << "  " << program_name
           << " agent [--debug|--no-debug] [--model <model>|<model>] [prompt]\n\n";
    output << "  " << program_name
           << " web-chat [--debug|--no-debug] [--model <model>|<model>] [--port <port>]\n\n";
    output << "Examples:\n";
    output << "  " << program_name << " train-demo\n";
    output << "  " << program_name
           << " agent qwen3:8b Use the calculator tool to compute (2 + 3) * 4.\n";
    output << "  " << program_name
           << " agent --debug qwen3:8b Search the workspace for ToolRegistry.\n";
    output << "  " << program_name
           << " web-chat --model qwen3:8b --port 8080\n";
    return output.str();
}
