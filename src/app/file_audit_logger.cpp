#include "file_audit_logger.h"

#include <ctime>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>

namespace {

std::string escape_log_value(const std::string& value) {
    std::string escaped;
    escaped.reserve(value.size());
    for (const char ch : value) {
        switch (ch) {
            case '\\':
                escaped += "\\\\";
                break;
            case '"':
                escaped += "\\\"";
                break;
            case '\n':
                escaped += "\\n";
                break;
            case '\r':
                escaped += "\\r";
                break;
            case '\t':
                escaped += "\\t";
                break;
            default:
                escaped += ch;
                break;
        }
    }
    return escaped;
}

std::string current_timestamp() {
    const std::time_t now = std::time(nullptr);
    std::tm local_time{};
#ifdef _WIN32
    localtime_s(&local_time, &now);
#else
    localtime_r(&now, &local_time);
#endif

    char buffer[32];
    if (std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%S", &local_time) == 0) {
        return "0000-00-00T00:00:00";
    }
    return buffer;
}

std::string format_event_line(const AuditEvent& event) {
    std::ostringstream output;
    output << "ts=\"" << current_timestamp() << "\"";
    output << " category=\"" << escape_log_value(event.category) << "\"";
    output << " stage=\"" << escape_log_value(event.stage) << "\"";
    output << " tool=\"" << escape_log_value(event.tool_name) << "\"";
    output << " target=\"" << escape_log_value(event.target) << "\"";
    output << " workspace=\"" << escape_log_value(event.workspace_root) << "\"";
    output << " timeout_ms=" << event.timeout_ms;
    output << " exit_code=" << event.exit_code;
    output << " approved=" << (event.approved ? "true" : "false");
    output << " success=" << (event.success ? "true" : "false");
    output << " timed_out=" << (event.timed_out ? "true" : "false");
    output << " detail=\"" << escape_log_value(event.detail) << "\"";
    return output.str();
}

}  // namespace

FileAuditLogger::FileAuditLogger(std::filesystem::path log_path)
    : log_path_(std::move(log_path)) {}

void FileAuditLogger::log(const AuditEvent& event) {
    std::lock_guard<std::mutex> guard(mutex_);

    const std::filesystem::path parent = log_path_.parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent);
    }

    std::ofstream output(log_path_, std::ios::app | std::ios::binary);
    if (!output) {
        throw std::runtime_error("Failed to open audit log: " + log_path_.string());
    }

    output << format_event_line(event) << "\n";
    if (!output.good()) {
        throw std::runtime_error("Failed to write audit log: " + log_path_.string());
    }
}
