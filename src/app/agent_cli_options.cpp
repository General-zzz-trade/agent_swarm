#include "agent_cli_options.h"

#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

struct AgentCliOverrides {
    std::optional<bool> debug;
    std::optional<std::string> model;
    std::string prompt;
};

std::string join_args(const std::vector<std::string>& args, std::size_t start_index) {
    std::ostringstream output;
    for (std::size_t i = start_index; i < args.size(); ++i) {
        if (i > start_index) {
            output << ' ';
        }
        output << args[i];
    }
    return output.str();
}

AgentCliOverrides parse_agent_cli_overrides(const std::vector<std::string>& args) {
    AgentCliOverrides overrides;

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
        if (current.rfind("--", 0) == 0) {
            throw std::invalid_argument("Unknown agent option: " + current);
        }
        if (!overrides.model.has_value() && current.find(':') != std::string::npos) {
            overrides.model = current;
            ++index;
            continue;
        }
        break;
    }

    if (index < args.size()) {
        overrides.prompt = join_args(args, index);
    }

    return overrides;
}

}  // namespace

std::vector<std::string> collect_cli_args(int argc, char* argv[], int start_index) {
    std::vector<std::string> args;
    for (int i = start_index; i < argc; ++i) {
        args.emplace_back(argv[i]);
    }
    return args;
}

AgentCliOptions resolve_agent_cli_options(const std::vector<std::string>& args,
                                          const AppConfig& config) {
    const AgentCliOverrides overrides = parse_agent_cli_overrides(args);

    AgentCliOptions options;
    options.debug = overrides.debug.value_or(config.agent_runtime.default_debug);
    options.model = overrides.model.value_or(config.default_model);
    options.prompt = overrides.prompt;
    return options;
}
