#ifndef APP_PROGRAM_CLI_H
#define APP_PROGRAM_CLI_H

#include <string>
#include <vector>

enum class TopLevelCommandType {
    usage,
    train_demo,
    agent,
    web_chat,
    telegram,
    discord,
    bench,
    mcp_server,
    api_server,
    invalid,
};

struct TopLevelCommand {
    TopLevelCommandType type;
    std::string command;
};

TopLevelCommand resolve_top_level_command(const std::vector<std::string>& args);
std::string build_usage_text(const std::string& program_name);

#endif
