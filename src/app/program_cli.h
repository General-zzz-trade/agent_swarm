#ifndef APP_PROGRAM_CLI_H
#define APP_PROGRAM_CLI_H

#include <string>
#include <vector>

enum class TopLevelCommandType {
    usage,
    version,
    train_demo,
    agent,
    web_chat,
    telegram,
    discord,
    wechat,
    slack,
    bench,
    mcp_server,
    api_server,
    doctor,
    init_workspace,
    invalid,
};

struct TopLevelCommand {
    TopLevelCommandType type;
    std::string command;
};

TopLevelCommand resolve_top_level_command(const std::vector<std::string>& args);
std::string build_usage_text(const std::string& program_name);

#endif
