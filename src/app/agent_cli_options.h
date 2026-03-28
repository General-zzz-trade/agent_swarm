#ifndef APP_AGENT_CLI_OPTIONS_H
#define APP_AGENT_CLI_OPTIONS_H

#include <string>
#include <vector>

#include "app_config.h"

struct AgentCliOptions {
    bool debug = false;
    std::string model;
    std::string prompt;
};

std::vector<std::string> collect_cli_args(int argc, char* argv[], int start_index);
AgentCliOptions resolve_agent_cli_options(const std::vector<std::string>& args,
                                          const AppConfig& config);

#endif
