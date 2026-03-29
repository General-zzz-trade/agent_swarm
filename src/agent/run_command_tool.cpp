#include "run_command_tool.h"

#include <cctype>
#include <algorithm>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "workspace_utils.h"

namespace {

struct CommandValidation {
    bool allowed;
    std::string message;
};

std::vector<std::string> tokenize_command(const std::string& command) {
    std::vector<std::string> tokens;
    std::string current;
    bool in_quotes = false;

    for (std::size_t i = 0; i < command.size(); ++i) {
        const char ch = command[i];
        if (ch == '"') {
            in_quotes = !in_quotes;
            continue;
        }

        if (!in_quotes && std::isspace(static_cast<unsigned char>(ch))) {
            if (!current.empty()) {
                tokens.push_back(current);
                current.clear();
            }
            continue;
        }

        current += ch;
    }

    if (in_quotes) {
        throw std::runtime_error("Unterminated quote in command");
    }

    if (!current.empty()) {
        tokens.push_back(current);
    }

    return tokens;
}

std::string join_sorted(const std::unordered_set<std::string>& values) {
    std::vector<std::string> sorted(values.begin(), values.end());
    std::sort(sorted.begin(), sorted.end());

    std::ostringstream output;
    for (std::size_t i = 0; i < sorted.size(); ++i) {
        if (i > 0) {
            output << ", ";
        }
        output << sorted[i];
    }
    return output.str();
}

std::string truncate_output(const std::string& output, std::size_t max_output_bytes) {
    if (output.size() <= max_output_bytes) {
        return output;
    }

    std::string truncated = output.substr(0, max_output_bytes);
    truncated += "\n[truncated ";
    truncated += std::to_string(output.size() - max_output_bytes);
    truncated += " bytes]";
    return truncated;
}

void safe_log_audit(const std::shared_ptr<IAuditLogger>& audit_logger, const AuditEvent& event) {
    if (audit_logger == nullptr) {
        return;
    }

    try {
        audit_logger->log(event);
    } catch (...) {
    }
}

CommandValidation validate_command(const std::string& command,
                                   const CommandPolicyConfig& config) {
    if (trim_copy(command).empty()) {
        return {false, "Command is empty"};
    }

    // Block only dangerous metacharacters; allow pipes and redirects for dev workflows
    for (const char ch : command) {
        switch (ch) {
            case '&':   // background execution — blocked
            case ';':   // command chaining — blocked (use && in shell)
            case '^':   // Windows escape — blocked
            case '\n':
            case '\r':
                return {false, "Command contains blocked shell metacharacters"};
            default:
                break;
        }
    }

    const std::vector<std::string> tokens = tokenize_command(command);
    if (tokens.empty()) {
        return {false, "Command is empty"};
    }

    const std::string& executable = tokens.front();
    if (config.allowed_executables.find(executable) == config.allowed_executables.end()) {
        return {false, "Executable is not in the whitelist"};
    }

    const auto subcommands_it = config.allowed_subcommands.find(executable);
    if (subcommands_it != config.allowed_subcommands.end()) {
        if (tokens.size() < 2) {
            return {false, executable + " requires a safe subcommand"};
        }
        if (subcommands_it->second.find(tokens[1]) == subcommands_it->second.end()) {
            return {false, executable + " subcommand is not allowed"};
        }
    }

    return {true, "Allowed"};
}

}  // namespace

RunCommandTool::RunCommandTool(std::filesystem::path workspace_root,
                               std::shared_ptr<ICommandRunner> command_runner,
                               std::shared_ptr<IAuditLogger> audit_logger,
                               CommandPolicyConfig config)
    : workspace_root_(std::filesystem::weakly_canonical(std::move(workspace_root))),
      command_runner_(std::move(command_runner)),
      audit_logger_(std::move(audit_logger)),
      config_(std::move(config)) {
    if (command_runner_ == nullptr) {
        throw std::invalid_argument("RunCommandTool requires a command runner");
    }
}

std::string RunCommandTool::name() const {
    return "run_command";
}

std::string RunCommandTool::description() const {
    return "Run a whitelist-limited developer command in the workspace. Allowed executables: " +
           join_sorted(config_.allowed_executables) + ".";
}

ToolPreview RunCommandTool::preview(const std::string& args) const {
    try {
        const std::string command = trim_copy(args);
        const CommandValidation validation = validate_command(command, config_);

        ToolPreview preview;
        preview.summary = "Run command: " + command;
        std::ostringstream details;
        details << "working directory: " << workspace_root_.string();
        details << "\ntimeout_ms: " << config_.timeout_ms;
        details << "\nmax_output_bytes: " << config_.max_output_bytes;
        if (!validation.allowed) {
            details << "\nvalidation: " << validation.message;
        }
        preview.details = details.str();
        return preview;
    } catch (const std::exception& e) {
        return {"Unable to preview run_command request", e.what()};
    }
}

ToolResult RunCommandTool::run(const std::string& args) const {
    try {
        const std::string command = trim_copy(args);
        const CommandValidation validation = validate_command(command, config_);
        if (!validation.allowed) {
            safe_log_audit(audit_logger_,
                           {"command", "validation_failed", name(), command,
                            workspace_root_.string(), config_.timeout_ms, -1, false, false,
                            false, validation.message});
            return {false, validation.message};
        }

        const CommandExecutionResult execution =
            command_runner_->run(command, workspace_root_, config_.timeout_ms);
        const std::string stdout_output =
            truncate_output(execution.stdout_output, config_.max_output_bytes);
        const std::string stderr_output =
            truncate_output(execution.stderr_output, config_.max_output_bytes);

        std::ostringstream result;
        result << "COMMAND: " << command << "\n";
        result << "EXIT_CODE: " << execution.exit_code << "\n";
        result << "TIMED_OUT: " << (execution.timed_out ? "true" : "false") << "\n";
        if (!stdout_output.empty()) {
            result << "[stdout]\n" << stdout_output;
            if (!stdout_output.empty() && stdout_output.back() != '\n') {
                result << "\n";
            }
        }
        if (!stderr_output.empty()) {
            result << "[stderr]\n" << stderr_output;
        }

        std::ostringstream detail;
        detail << "stdout_bytes=" << execution.stdout_output.size()
               << " stderr_bytes=" << execution.stderr_output.size();
        safe_log_audit(audit_logger_,
                       {"command", "executed", name(), command, workspace_root_.string(),
                        config_.timeout_ms, execution.exit_code, true, execution.success,
                        execution.timed_out, detail.str()});

        return {execution.success, result.str()};
    } catch (const std::exception& e) {
        safe_log_audit(audit_logger_,
                       {"command", "tool_error", name(), trim_copy(args),
                        workspace_root_.string(), config_.timeout_ms, -1, false, false, false,
                        e.what()});
        return {false, e.what()};
    }
}
