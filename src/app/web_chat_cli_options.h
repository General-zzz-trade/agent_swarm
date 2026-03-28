#ifndef APP_WEB_CHAT_CLI_OPTIONS_H
#define APP_WEB_CHAT_CLI_OPTIONS_H

#include <string>
#include <vector>

#include "app_config.h"

struct WebChatCliOptions {
    bool debug = false;
    std::string model;
    unsigned short port = 8080;
};

WebChatCliOptions resolve_web_chat_cli_options(const std::vector<std::string>& args,
                                               const AppConfig& config);

#endif
