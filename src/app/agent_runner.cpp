#include "agent_runner.h"

#include <atomic>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <ostream>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <unistd.h>

#include "../agent/agent.h"
#include "../core/session/session_store.h"
#include "setup_wizard.h"
#include "terminal_renderer.h"
#include "terminal_input.h"
#include "signal_handler.h"
#include "token_tracker.h"

namespace {

// Animated spinner shown while the model is thinking
class Spinner {
public:
    void start(std::ostream& output) {
        running_ = true;
        thread_ = std::thread([this, &output]() {
            const char* frames[] = {"⠋", "⠙", "⠹", "⠸", "⠼", "⠴", "⠦", "⠧", "⠇", "⠏"};
            int i = 0;
            while (running_) {
                output << "\r\033[2m" << frames[i % 10] << " Thinking...\033[0m   " << std::flush;
                std::this_thread::sleep_for(std::chrono::milliseconds(80));
                ++i;
            }
            output << "\r                     \r" << std::flush;
        });
    }

    void stop() {
        running_ = false;
        if (thread_.joinable()) thread_.join();
    }

private:
    std::atomic<bool> running_{false};
    std::thread thread_;
};

// @file reference expansion
std::string expand_file_references(const std::string& input,
                                    const std::filesystem::path& workspace_root) {
    if (workspace_root.empty()) return input;

    std::string result;
    std::size_t pos = 0;
    while (pos < input.size()) {
        if (input[pos] == '@' && pos + 1 < input.size() &&
            (std::isalnum(input[pos + 1]) || input[pos + 1] == '.' || input[pos + 1] == '/')) {
            // Parse file path
            std::size_t start = pos + 1;
            std::size_t end = start;
            while (end < input.size() && !std::isspace(input[end]) &&
                   input[end] != ',' && input[end] != ')') {
                ++end;
            }
            std::string ref = input.substr(start, end - start);

            // Check for line range  @file:10-20
            int start_line = 0, end_line = 0;
            auto colon = ref.find(':');
            std::string path_str = ref;
            if (colon != std::string::npos) {
                path_str = ref.substr(0, colon);
                std::string range = ref.substr(colon + 1);
                auto dash = range.find('-');
                if (dash != std::string::npos) {
                    try {
                        start_line = std::stoi(range.substr(0, dash));
                        end_line = std::stoi(range.substr(dash + 1));
                    } catch (...) {}
                } else {
                    try { start_line = end_line = std::stoi(range); } catch (...) {}
                }
            }

            auto full_path = workspace_root / path_str;
            if (std::filesystem::exists(full_path) && std::filesystem::is_regular_file(full_path)) {
                std::ifstream file(full_path);
                std::string content;
                if (start_line > 0) {
                    // Read specific lines
                    std::string line;
                    int ln = 0;
                    while (std::getline(file, line)) {
                        ++ln;
                        if (ln >= start_line && (end_line == 0 || ln <= end_line)) {
                            content += line + "\n";
                        }
                        if (end_line > 0 && ln > end_line) break;
                    }
                } else {
                    content.assign(std::istreambuf_iterator<char>(file),
                                   std::istreambuf_iterator<char>());
                }

                result += "\n<file path=\"" + path_str + "\"";
                if (start_line > 0) {
                    result += " lines=\"" + std::to_string(start_line);
                    if (end_line > 0 && end_line != start_line) result += "-" + std::to_string(end_line);
                    result += "\"";
                }
                result += ">\n" + content + "</file>\n";
                pos = end;
            } else {
                result += input[pos];
                ++pos;
            }
        } else {
            result += input[pos];
            ++pos;
        }
    }
    return result;
}

// Undo stack for file edits
struct UndoEntry {
    std::string path;
    std::string original_content;
};

}  // namespace

std::string build_agent_banner(const Agent& agent) {
    std::ostringstream output;
    output << "\n\033[1;36m ⚡ Bolt\033[0m — AI Coding Agent\n";
    output << "\033[2m   Model: " << agent.model() << "\n";
    output << "   Debug: " << (agent.debug_enabled() ? "on" : "off") << "\n";
    output << "   Tips:  /help for commands, @file to include files, Ctrl+C to cancel\033[0m\n";
    return output.str();
}

int run_agent_single_turn(Agent& agent, const std::string& prompt, std::ostream& output) {
    bool first_token = true;
    bool is_terminal = (&output == &std::cout);

    Spinner spinner;
    if (is_terminal) spinner.start(output);

    TerminalRenderer renderer(output);
    renderer.begin_stream();

    std::string reply = agent.run_turn_streaming(prompt,
        [&](const std::string& token) {
            if (first_token) {
                if (is_terminal) spinner.stop();
                first_token = false;
            }
            renderer.stream_token(token);
        });

    if (first_token) {
        if (is_terminal) spinner.stop();
        renderer.render_markdown(reply);
    }
    renderer.end_stream();
    output << std::endl;
    return 0;
}

int run_agent_interactive_loop(Agent& agent, std::istream& /*input*/, std::ostream& output,
                               const std::filesystem::path& workspace_root,
                               bool resume_last) {
    // Initialize components
    TerminalRenderer renderer(output);
    TerminalInput term_input(STDIN_FILENO, output);
    SignalHandler::instance().install();
    TokenTracker tracker;

    // Undo stack
    std::vector<UndoEntry> undo_stack;

    // Configure tab completion
    std::vector<std::string> slash_commands = {
        "/help", "/quit", "/exit", "/q",
        "/clear", "/compact", "/model", "/cost",
        "/debug", "/save", "/load", "/sessions", "/delete",
        "/export", "/undo", "/diff", "/status", "/reset",
        "/sandbox"
    };
    term_input.set_slash_commands(slash_commands);
    if (!workspace_root.empty()) {
        term_input.set_workspace_root(workspace_root);
    }

    // Session support
    std::unique_ptr<SessionStore> sessions;
    std::string current_session_id;
    if (!workspace_root.empty()) {
        sessions = std::make_unique<SessionStore>(workspace_root / ".bolt" / "sessions");
        current_session_id = SessionStore::generate_id();

        // Resume last session if requested
        if (resume_last) {
            auto list = sessions->list();
            if (!list.empty()) {
                auto msgs = sessions->load(list[0].id);
                if (!msgs.empty()) {
                    agent.restore_history(msgs);
                    current_session_id = list[0].id;
                    output << "\033[2mResumed session: " << current_session_id
                           << " (" << msgs.size() << " messages)\033[0m\n";
                }
            }
        }
    }

    // Install cancellation check
    agent.set_cancellation_check([&]() -> bool {
        return SignalHandler::instance().is_cancelled();
    });

    // Render banner
    renderer.render_banner(agent.model(), agent.debug_enabled());

    // Auto-save helper
    auto auto_save = [&]() {
        if (sessions) {
            auto msgs = agent.get_chat_messages();
            if (!msgs.empty()) {
                sessions->save(current_session_id, msgs);
            }
        }
    };

    // Main loop
    while (true) {
        // Check for terminal resize
        if (SignalHandler::instance().resize_pending()) {
            SignalHandler::instance().clear_resize_pending();
            renderer.update_terminal_width();
        }

        // Determine prompt color
        std::string prompt = "\033[1;32m❯ \033[0m";

        SignalHandler::instance().reset();
        auto line_opt = term_input.read_line(prompt);

        if (!line_opt.has_value()) {
            // Ctrl+D
            output << "\033[2mGoodbye!\033[0m\n";
            auto_save();
            break;
        }

        std::string line = line_opt.value();

        if (term_input.cancelled()) {
            continue;
        }

        // Trim
        auto trim = [](const std::string& s) -> std::string {
            auto b = s.find_first_not_of(" \t\r\n");
            if (b == std::string::npos) return "";
            auto e = s.find_last_not_of(" \t\r\n");
            return s.substr(b, e - b + 1);
        };
        line = trim(line);
        if (line.empty()) continue;

        // === Slash Commands ===

        if (line == "/quit" || line == "/exit" || line == "/q") {
            output << "\033[2mGoodbye!\033[0m\n";
            auto_save();
            break;
        }

        if (line == "/clear") {
            agent.clear_history();
            tracker = TokenTracker();
            output << "\033[2mHistory and token counts cleared.\033[0m\n";
            continue;
        }

        if (line == "/help") {
            output << "\n\033[1;35m Session\033[0m\n";
            output << "  \033[1m/save\033[0m \033[2m[name]\033[0m     Save current session\n";
            output << "  \033[1m/load\033[0m \033[2m<id>\033[0m       Load a saved session\n";
            output << "  \033[1m/sessions\033[0m          List saved sessions\n";
            output << "  \033[1m/delete\033[0m \033[2m<id>\033[0m     Delete a session\n";
            output << "  \033[1m/export\033[0m \033[2m[file]\033[0m   Export chat to markdown\n";
            output << "\n\033[1;35m Context\033[0m\n";
            output << "  \033[1m/clear\033[0m             Clear conversation history\n";
            output << "  \033[1m/compact\033[0m           Compress context to save tokens\n";
            output << "  \033[1m/undo\033[0m              Revert last file edit\n";
            output << "  \033[1m/reset\033[0m             Full reset (history + index)\n";
            output << "\n\033[1;35m Display\033[0m\n";
            output << "  \033[1m/model\033[0m \033[2m[name]\033[0m    Show or switch model\n";
            output << "  \033[1m/cost\033[0m              Show token usage and cost\n";
            output << "  \033[1m/status\033[0m            Show current status\n";
            output << "  \033[1m/debug\033[0m             Toggle debug mode\n";
            output << "  \033[1m/diff\033[0m              Show git diff\n";
            output << "  \033[1m/sandbox\033[0m           Show sandbox status\n";
            output << "\n\033[1;35m System\033[0m\n";
            output << "  \033[1m/quit\033[0m              Exit Bolt\n";
            output << "\n\033[1;35m Shortcuts\033[0m\n";
            output << "  \033[2mCtrl+C\033[0m cancel  \033[2mCtrl+L\033[0m clear screen  \033[2mCtrl+D\033[0m exit\n";
            output << "  \033[2m↑/↓\033[0m history   \033[2mTab\033[0m complete       \033[2m@file\033[0m include file\n";
            output << "\n";
            continue;
        }

        if (line == "/compact") {
            agent.compact_history();
            output << "\033[2mContext compacted.\033[0m\n";
            continue;
        }

        if (line == "/model" || line.rfind("/model ", 0) == 0) {
            if (line == "/model") {
                // Interactive model selector
                output << "\033[2mCurrent: " << agent.model() << "\033[0m\n";
                output << "\033[2mNote: Model switching takes effect on next launch.\033[0m\n";
                output << "\033[2mUse /model <name> to set, or run without args for selector.\033[0m\n\n";

                // Run interactive selector
                SetupResult result = run_model_selector("", agent.model());
                if (result.completed) {
                    save_setup_config(result);
                    output << "\n\033[32m✓ Saved! New model will be used on next launch.\033[0m\n";
                    output << "\033[2m  Provider: " << result.provider << "\n";
                    output << "  Model:    " << result.model << "\033[0m\n";
                }
            } else {
                // Direct set: /model claude-sonnet-4-20250514
                std::string model_name = line.substr(7);
                // trim
                auto b = model_name.find_first_not_of(" \t");
                if (b != std::string::npos) model_name = model_name.substr(b);
                auto e = model_name.find_last_not_of(" \t");
                if (e != std::string::npos) model_name = model_name.substr(0, e + 1);

                // Detect provider from model name
                SetupResult result;
                result.model = model_name;
                if (model_name.find("claude") != std::string::npos) {
                    result.provider = "claude";
                } else if (model_name.find("gpt") != std::string::npos ||
                           model_name.find("o3") != std::string::npos) {
                    result.provider = "openai";
                } else if (model_name.find("gemini") != std::string::npos) {
                    result.provider = "gemini";
                } else if (model_name.find("llama") != std::string::npos ||
                           model_name.find("mixtral") != std::string::npos) {
                    if (std::getenv("GROQ_API_KEY")) {
                        result.provider = "groq";
                    } else {
                        result.provider = "ollama-chat";
                    }
                } else {
                    result.provider = "ollama-chat";  // default to Ollama for unknown models
                }
                result.completed = true;
                save_setup_config(result);
                output << "\033[32m✓ Saved for next launch: " << result.provider
                       << " / " << result.model << "\033[0m\n";
            }
            continue;
        }

        if (line == "/cost") {
            auto total = tracker.total();
            renderer.render_cost_summary(agent.model(), total.input_tokens, total.output_tokens,
                                         tracker.estimated_cost(), tracker.turn_count());
            continue;
        }

        if (line == "/debug") {
            bool new_debug = !agent.debug_enabled();
            agent.set_debug(new_debug);
            output << "\033[2mDebug: " << (new_debug ? "on" : "off") << "\033[0m\n";
            continue;
        }

        if ((line == "/save" || line.rfind("/save ", 0) == 0) && sessions) {
            std::string save_id = current_session_id;
            if (line.size() > 6) {
                save_id = trim(line.substr(6));
                current_session_id = save_id;
            }
            auto msgs = agent.get_chat_messages();
            if (msgs.empty()) {
                output << "\033[33mNo messages to save.\033[0m\n";
            } else if (sessions->save(save_id, msgs)) {
                output << "\033[2mSession saved: " << save_id << " (" << msgs.size() << " messages)\033[0m\n";
            } else {
                output << "\033[31mFailed to save session.\033[0m\n";
            }
            continue;
        }

        if (line.rfind("/load ", 0) == 0 && sessions) {
            std::string id = trim(line.substr(6));
            auto msgs = sessions->load(id);
            if (msgs.empty()) {
                output << "\033[33mSession not found: " << id << "\033[0m\n";
            } else {
                agent.restore_history(msgs);
                current_session_id = id;
                tracker = TokenTracker(); // reset tracker
                output << "\033[2mLoaded session: " << id << " (" << msgs.size() << " messages)\033[0m\n";
            }
            continue;
        }

        if (line == "/sessions" && sessions) {
            auto list = sessions->list();
            if (list.empty()) {
                output << "\033[2mNo saved sessions.\033[0m\n";
            } else {
                std::vector<std::tuple<std::string, int, std::string, std::string>> session_data;
                for (const auto& s : list) {
                    session_data.emplace_back(s.id, static_cast<int>(s.message_count),
                                              s.modified_at, s.last_message);
                }
                renderer.render_sessions_list(session_data);
            }
            continue;
        }

        if (line.rfind("/delete ", 0) == 0 && sessions) {
            std::string id = trim(line.substr(8));
            if (sessions->remove(id)) {
                output << "\033[2mDeleted session: " << id << "\033[0m\n";
            } else {
                output << "\033[33mSession not found: " << id << "\033[0m\n";
            }
            continue;
        }

        if (line == "/export" || line.rfind("/export ", 0) == 0) {
            std::string filename;
            if (line.size() > 8) {
                filename = trim(line.substr(8));
            } else {
                // Generate default filename
                auto now = std::chrono::system_clock::now();
                auto time = std::chrono::system_clock::to_time_t(now);
                std::tm tm_buf{};
                localtime_r(&time, &tm_buf);
                std::ostringstream ss;
                ss << "bolt-export-" << std::put_time(&tm_buf, "%Y%m%d-%H%M%S") << ".md";
                filename = ss.str();
            }
            auto msgs = agent.get_chat_messages();
            if (msgs.empty()) {
                output << "\033[33mNo messages to export.\033[0m\n";
            } else {
                std::ofstream f(filename);
                if (f) {
                    f << "# Bolt Chat Export\n\n";
                    for (const auto& msg : msgs) {
                        if (msg.role == ChatRole::user) {
                            f << "## User\n\n" << msg.content << "\n\n";
                        } else if (msg.role == ChatRole::assistant) {
                            f << "## Assistant\n\n" << msg.content << "\n\n";
                        } else if (msg.role == ChatRole::tool) {
                            f << "### Tool: " << msg.name << "\n\n```\n" << msg.content << "\n```\n\n";
                        }
                    }
                    output << "\033[2mExported to: " << filename << "\033[0m\n";
                } else {
                    output << "\033[31mFailed to write: " << filename << "\033[0m\n";
                }
            }
            continue;
        }

        if (line == "/undo") {
            if (undo_stack.empty()) {
                output << "\033[33mNothing to undo.\033[0m\n";
            } else {
                auto& entry = undo_stack.back();
                std::ofstream f(entry.path);
                if (f) {
                    f << entry.original_content;
                    output << "\033[2mReverted: " << entry.path << "\033[0m\n";
                } else {
                    output << "\033[31mFailed to revert: " << entry.path << "\033[0m\n";
                }
                undo_stack.pop_back();
            }
            continue;
        }

        if (line == "/diff") {
            // Run git diff and display
            FILE* pipe = popen("git diff 2>&1", "r");
            if (pipe) {
                std::string diff_output;
                char buffer[256];
                while (fgets(buffer, sizeof(buffer), pipe)) {
                    diff_output += buffer;
                }
                pclose(pipe);
                if (diff_output.empty()) {
                    output << "\033[2mNo changes.\033[0m\n";
                } else {
                    renderer.render_diff(diff_output);
                }
            }
            continue;
        }

        if (line == "/status") {
            auto total = tracker.total();
            renderer.render_status_info(agent.model(), total.input_tokens, total.output_tokens,
                                        current_session_id, workspace_root.string(),
                                        static_cast<int>(agent.file_index().file_count()));
            continue;
        }

        if (line == "/reset") {
            agent.clear_history();
            tracker = TokenTracker();
            undo_stack.clear();
            current_session_id = SessionStore::generate_id();
            output << "\033[2mFull reset complete.\033[0m\n";
            continue;
        }

        if (line == "/sandbox") {
            output << "\n\033[1;35m Sandbox Status\033[0m\n\n";
            bool bwrap_available = std::filesystem::exists("/usr/bin/bwrap") ||
                                   std::filesystem::exists("/usr/local/bin/bwrap");
            output << "  Bubblewrap: " << (bwrap_available ? "\033[32mavailable\033[0m" : "\033[31mnot found\033[0m") << "\n";
            output << "\n  \033[2mConfigure in bolt.conf or env:\033[0m\n";
            output << "    sandbox.enabled = true\n";
            output << "    sandbox.network_enabled = true\n";
            output << "    sandbox.deny_read = ~/.ssh,~/.aws\n";
            output << "    sandbox.allow_write = /tmp/build\n";
            output << "\n  \033[2mOr: BOLT_SANDBOX_ENABLED=true bolt agent\033[0m\n\n";
            if (!bwrap_available) {
                output << "  \033[33mInstall: sudo apt install bubblewrap\033[0m\n\n";
            }
            continue;
        }

        // Unknown slash command
        if (line[0] == '/') {
            output << "\033[33mUnknown command: " << line << ". Type /help for available commands.\033[0m\n";
            continue;
        }

        // === Regular prompt ===

        // Capture file state for undo (for any files referenced by @)
        // Also expand @file references
        std::string expanded = expand_file_references(line, workspace_root);

        // Track files that might be edited (for undo support)
        // Hook into trace observer to capture edit operations
        agent.set_trace_observer([&](const std::vector<ExecutionStep>& trace) {
            for (const auto& step : trace) {
                if ((step.tool_name == "edit_file" || step.tool_name == "write_file") &&
                    step.status == ExecutionStepStatus::planned) {
                    // Try to capture file state before edit
                    auto path_start = step.args.find("path=");
                    if (path_start != std::string::npos) {
                        auto path_end = step.args.find('\n', path_start);
                        std::string rel_path = step.args.substr(path_start + 5,
                            path_end == std::string::npos ? std::string::npos : path_end - path_start - 5);
                        // Trim
                        while (!rel_path.empty() && (rel_path.back() == ' ' || rel_path.back() == '\r'))
                            rel_path.pop_back();

                        auto full_path = workspace_root / rel_path;
                        if (std::filesystem::exists(full_path)) {
                            std::ifstream f(full_path);
                            std::string content(std::istreambuf_iterator<char>(f), {});
                            // Only keep last 20 undo entries
                            if (undo_stack.size() >= 20) undo_stack.erase(undo_stack.begin());
                            undo_stack.push_back({full_path.string(), content});
                        }
                    }
                }
            }
        });

        // Stream the response
        Spinner spinner;
        bool first_token = true;

        SignalHandler::instance().reset();
        spinner.start(output);
        renderer.begin_stream();

        try {
            std::string reply = agent.run_turn_streaming(expanded,
                [&](const std::string& token) {
                    if (first_token) {
                        spinner.stop();
                        first_token = false;
                        output << "\n";
                    }
                    renderer.stream_token(token);
                });

            if (first_token) {
                spinner.stop();
                output << "\n";
                renderer.render_markdown(reply);
            }
            renderer.end_stream();
            output << "\n";

            // Record token usage
            auto usage = agent.last_token_usage();
            tracker.record_turn(usage, agent.model());

            // Update status bar
            auto total = tracker.total();
            renderer.render_status_bar(agent.model(), total.input_tokens, total.output_tokens,
                                       current_session_id);

        } catch (const std::exception& e) {
            if (first_token) spinner.stop();
            renderer.end_stream();
            output << "\n\033[31mError: " << e.what() << "\033[0m\n";
        }

        // Auto-save
        auto_save();
    }

    return 0;
}
