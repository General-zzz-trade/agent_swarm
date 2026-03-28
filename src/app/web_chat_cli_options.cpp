#include "web_chat_cli_options.h"

#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

struct WebChatCliOverrides {
    std::optional<bool> debug;
    std::optional<std::string> model;
    std::optional<unsigned short> port;
};

unsigned short parse_port_value(const std::string& value) {
    try {
        const int parsed = std::stoi(value);
        if (parsed <= 0 || parsed > 65535) {
            throw std::invalid_argument("out of range");
        }
        return static_cast<unsigned short>(parsed);
    } catch (const std::exception&) {
        throw std::invalid_argument("--port requires a value between 1 and 65535");
    }
}

WebChatCliOverrides parse_web_chat_cli_overrides(const std::vector<std::string>& args) {
    WebChatCliOverrides overrides;

    std::size_t index = 0;
    while (index < args.size()) {
        const std::string& current = args[index];
        if (current == "--debug") {
            overrides.debug = true;
            ++index;
            continue;
        }
        if (current == "--no-debug") {
            overrides.debug = false;
            ++index;
            continue;
        }
        if (current == "--model") {
            if (index + 1 >= args.size()) {
                throw std::invalid_argument("--model requires a value");
            }
            overrides.model = args[index + 1];
            index += 2;
            continue;
        }
        if (current == "--port") {
            if (index + 1 >= args.size()) {
                throw std::invalid_argument("--port requires a value");
            }
            overrides.port = parse_port_value(args[index + 1]);
            index += 2;
            continue;
        }
        if (current.rfind("--", 0) == 0) {
            throw std::invalid_argument("Unknown web-chat option: " + current);
        }
        if (!overrides.model.has_value() && current.find(':') != std::string::npos) {
            overrides.model = current;
            ++index;
            continue;
        }

        throw std::invalid_argument("Unexpected positional argument for web-chat: " + current);
    }

    return overrides;
}

}  // namespace

WebChatCliOptions resolve_web_chat_cli_options(const std::vector<std::string>& args,
                                               const AppConfig& config) {
    const WebChatCliOverrides overrides = parse_web_chat_cli_overrides(args);

    WebChatCliOptions options;
    options.debug = overrides.debug.value_or(config.agent_runtime.default_debug);
    options.model = overrides.model.value_or(config.default_model);
    options.port = overrides.port.value_or(8080);
    return options;
}
