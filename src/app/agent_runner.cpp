#include "agent_runner.h"

#include <ostream>
#include <sstream>
#include <string>

#include "../agent/agent.h"

std::string build_agent_banner(const Agent& agent) {
    std::ostringstream output;
    output << "Agent mode. Model: " << agent.model() << "\n";
    output << "Debug: " << (agent.debug_enabled() ? "on" : "off") << "\n";
    output << "Commands: /quit, /clear\n";
    return output.str();
}

int run_agent_single_turn(Agent& agent, const std::string& prompt, std::ostream& output) {
    output << agent.run_turn(prompt) << std::endl;
    return 0;
}

int run_agent_interactive_loop(Agent& agent, std::istream& input, std::ostream& output) {
    output << build_agent_banner(agent);

    std::string line;
    while (true) {
        output << "\n> ";
        output.flush();

        if (!std::getline(input, line)) {
            break;
        }
        if (line == "/quit") {
            break;
        }
        if (line == "/clear") {
            agent.clear_history();
            output << "History cleared.\n";
            continue;
        }
        if (line.empty()) {
            continue;
        }

        output << agent.run_turn(line) << std::endl;
    }

    return 0;
}
