#include "agent_runner.h"

#include <atomic>
#include <chrono>
#include <iostream>
#include <ostream>
#include <sstream>
#include <string>
#include <thread>

#include "../agent/agent.h"

namespace {

// ANSI color codes
const char* const kBold    = "\033[1m";
const char* const kDim     = "\033[2m";
const char* const kCyan    = "\033[36m";
const char* const kGreen   = "\033[32m";
const char* const kYellow  = "\033[33m";
const char* const kReset   = "\033[0m";

// Animated spinner shown while the model is thinking
class Spinner {
public:
    void start(std::ostream& output) {
        running_ = true;
        thread_ = std::thread([this, &output]() {
            const char* frames[] = {"⠋", "⠙", "⠹", "⠸", "⠼", "⠴", "⠦", "⠧", "⠇", "⠏"};
            int i = 0;
            while (running_) {
                output << "\r" << kDim << frames[i % 10] << " Thinking..." << kReset << "   " << std::flush;
                std::this_thread::sleep_for(std::chrono::milliseconds(80));
                ++i;
            }
            // Clear spinner line
            output << "\r                     \r" << std::flush;
        });
    }

    void stop() {
        running_ = false;
        if (thread_.joinable()) {
            thread_.join();
        }
    }

private:
    std::atomic<bool> running_{false};
    std::thread thread_;
};

}  // namespace

std::string build_agent_banner(const Agent& agent) {
    std::ostringstream output;
    output << "\n" << kBold << kCyan << "⚡ Bolt" << kReset << " — AI Coding Agent\n";
    output << kDim << "  Model: " << agent.model() << "\n";
    output << "  Debug: " << (agent.debug_enabled() ? "on" : "off") << "\n";
    output << "  Commands: /quit  /clear  /help" << kReset << "\n";
    return output.str();
}

int run_agent_single_turn(Agent& agent, const std::string& prompt, std::ostream& output) {
    // Use streaming for real-time token output
    bool first_token = true;
    bool is_terminal = (&output == &std::cout);

    // Only show spinner on real terminal (not in tests with stringstream)
    Spinner spinner;
    if (is_terminal) spinner.start(output);

    std::string reply = agent.run_turn_streaming(prompt,
        [&](const std::string& token) {
            if (first_token) {
                if (is_terminal) spinner.stop();
                first_token = false;
            }
            output << token << std::flush;
        });

    if (first_token) {
        if (is_terminal) spinner.stop();
        output << reply;
    }
    output << std::endl;
    return 0;
}

int run_agent_interactive_loop(Agent& agent, std::istream& input, std::ostream& output) {
    output << build_agent_banner(agent);

    std::string line;
    while (true) {
        output << "\n" << kBold << kGreen << "❯ " << kReset;
        output.flush();

        if (!std::getline(input, line)) {
            break;
        }
        if (line == "/quit" || line == "/exit" || line == "/q") {
            output << kDim << "Goodbye!" << kReset << "\n";
            break;
        }
        if (line == "/clear") {
            agent.clear_history();
            output << kDim << "History cleared." << kReset << "\n";
            continue;
        }
        if (line == "/help") {
            output << kDim
                   << "Commands:\n"
                   << "  /quit     Exit Bolt\n"
                   << "  /clear    Clear conversation history\n"
                   << "  /help     Show this help\n\n"
                   << "Examples:\n"
                   << "  Read src/main.cpp and explain it\n"
                   << "  Search for TODO comments in the codebase\n"
                   << "  Add error handling to the login function\n"
                   << "  Run build_and_test to verify everything compiles\n"
                   << kReset;
            continue;
        }
        if (line.empty()) {
            continue;
        }

        // Stream the response with spinner
        Spinner spinner;
        bool first_token = true;

        spinner.start(output);
        std::string reply = agent.run_turn_streaming(line,
            [&](const std::string& token) {
                if (first_token) {
                    spinner.stop();
                    first_token = false;
                    output << "\n";
                }
                output << token << std::flush;
            });

        if (first_token) {
            spinner.stop();
            output << "\n" << reply;
        }
        output << std::endl;
    }

    return 0;
}
