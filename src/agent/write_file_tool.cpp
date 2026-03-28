#include "write_file_tool.h"

#include <filesystem>
#include <sstream>
#include <stdexcept>

#include "workspace_utils.h"

namespace {

struct WriteFileRequest {
    std::string path;
    std::string content;
};

std::string normalize_newlines(const std::string& value) {
    std::string normalized;
    normalized.reserve(value.size());

    for (std::size_t i = 0; i < value.size(); ++i) {
        if (value[i] == '\r') {
            if (i + 1 < value.size() && value[i + 1] == '\n') {
                continue;
            }
            normalized += '\n';
            continue;
        }
        normalized += value[i];
    }

    return normalized;
}

std::string decode_escaped_newlines(const std::string& value) {
    std::string decoded;
    decoded.reserve(value.size());

    for (std::size_t i = 0; i < value.size(); ++i) {
        if (value[i] == '\\' && i + 1 < value.size()) {
            const char next = value[i + 1];
            if (next == 'n') {
                decoded += '\n';
                ++i;
                continue;
            }
            if (next == 'r') {
                ++i;
                continue;
            }
            if (next == 't') {
                decoded += '\t';
                ++i;
                continue;
            }
        }
        decoded += value[i];
    }

    return decoded;
}

WriteFileRequest parse_write_request(const std::string& raw_args) {
    std::string args = normalize_newlines(raw_args);
    if (args.find("\ncontent<<<") == std::string::npos && args.find("\\n") != std::string::npos) {
        args = decode_escaped_newlines(args);
    }

    const auto line_end = args.find('\n');
    const std::string first_line = line_end == std::string::npos ? args : args.substr(0, line_end);

    std::string path;
    if (first_line.rfind("path=", 0) == 0) {
        path = trim_copy(first_line.substr(5));
    } else if (first_line.rfind("path:", 0) == 0) {
        path = trim_copy(first_line.substr(5));
    } else {
        throw std::runtime_error("write_file args must start with 'path=' or 'path:'");
    }

    if (path.empty()) {
        throw std::runtime_error("write_file path is empty");
    }

    const std::string content_marker = "\ncontent<<<";
    const auto content_start = args.find(content_marker);
    if (content_start == std::string::npos) {
        throw std::runtime_error("write_file args must contain 'content<<<'");
    }

    std::size_t content_offset = content_start + content_marker.size();
    if (content_offset < args.size() && args[content_offset] == '\n') {
        ++content_offset;
    }

    std::size_t end_marker = args.rfind("\n>>>");
    std::size_t end_marker_width = 4;
    if (end_marker == std::string::npos || end_marker < content_offset) {
        end_marker = args.rfind(">>>");
        end_marker_width = 3;
    }
    if (end_marker == std::string::npos || end_marker < content_offset) {
        throw std::runtime_error("write_file args must end with '>>>'");
    }

    std::string content = args.substr(content_offset, end_marker - content_offset);
    if (!content.empty() && content.back() == '\n' && end_marker_width == 4) {
        content.pop_back();
    }

    return {path, content};
}

std::string shorten_preview(const std::string& value, std::size_t max_length) {
    if (value.size() <= max_length) {
        return value;
    }
    return value.substr(0, max_length) + "...";
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

}  // namespace

WriteFileTool::WriteFileTool(std::filesystem::path workspace_root,
                             std::shared_ptr<IFileSystem> file_system,
                             std::shared_ptr<IAuditLogger> audit_logger)
    : workspace_root_(std::filesystem::weakly_canonical(std::move(workspace_root))),
      file_system_(std::move(file_system)),
      audit_logger_(std::move(audit_logger)) {
    if (file_system_ == nullptr) {
        throw std::invalid_argument("WriteFileTool requires a file system");
    }
}

std::string WriteFileTool::name() const {
    return "write_file";
}

std::string WriteFileTool::description() const {
    return "Write a text file inside the workspace. Args format: path=<relative path> then content<<< ... >>>";
}

ToolPreview WriteFileTool::preview(const std::string& args) const {
    try {
        const WriteFileRequest request = parse_write_request(args);
        const std::filesystem::path target =
            resolve_workspace_path(workspace_root_, request.path);

        ToolPreview preview;
        preview.summary = "Write file " + workspace_relative_path(workspace_root_, target) +
                          " (" + std::to_string(request.content.size()) + " bytes)";
        preview.details = "path: " + workspace_relative_path(workspace_root_, target) +
                          "\ncontent preview:\n" + shorten_preview(request.content, 240);
        return preview;
    } catch (const std::exception& e) {
        return {"Unable to preview write_file request", e.what()};
    }
}

ToolResult WriteFileTool::run(const std::string& args) const {
    try {
        const WriteFileRequest request = parse_write_request(args);
        const std::filesystem::path target =
            resolve_workspace_path(workspace_root_, request.path);
        const std::string relative_path = workspace_relative_path(workspace_root_, target);
        if (request.content.size() > 100000) {
            safe_log_audit(audit_logger_,
                           {"file", "validation_failed", name(), relative_path,
                            workspace_root_.string(), 0, -1, false, false, false,
                            "Refusing to write files larger than 100000 bytes"});
            return {false, "Refusing to write files larger than 100000 bytes"};
        }

        if (!is_within_workspace(target, workspace_root_)) {
            safe_log_audit(audit_logger_,
                           {"file", "validation_failed", name(), request.path,
                            workspace_root_.string(), 0, -1, false, false, false,
                            "Path is outside the workspace"});
            return {false, "Path is outside the workspace"};
        }

        if (file_system_->exists(target) && file_system_->is_directory(target)) {
            safe_log_audit(audit_logger_,
                           {"file", "validation_failed", name(), relative_path,
                            workspace_root_.string(), 0, -1, false, false, false,
                            "Target path is a directory"});
            return {false, "Target path is a directory"};
        }

        const std::filesystem::path parent = target.parent_path();
        std::string create_error;
        if (!file_system_->create_directories(parent, create_error)) {
            safe_log_audit(audit_logger_,
                           {"file", "tool_error", name(), relative_path,
                            workspace_root_.string(), 0, -1, false, false, false,
                            create_error});
            return {false, create_error};
        }

        const FileWriteResult write_result = file_system_->write_text_file(target, request.content);
        if (!write_result.success) {
            safe_log_audit(audit_logger_,
                           {"file", "tool_error", name(), relative_path,
                            workspace_root_.string(), 0, -1, false, false, false,
                            write_result.error});
            return {false, write_result.error};
        }

        std::string preview = request.content.substr(0, 120);
        if (preview.size() < request.content.size()) {
            preview += "...";
        }

        safe_log_audit(audit_logger_,
                       {"file", "executed", name(), relative_path, workspace_root_.string(),
                        0, -1, true, true, false,
                        "bytes=" + std::to_string(request.content.size())});

        return {true,
                "WROTE FILE: " + relative_path +
                    "\nBytes: " + std::to_string(request.content.size()) +
                    "\nPreview:\n" + preview};
    } catch (const std::exception& e) {
        safe_log_audit(audit_logger_,
                       {"file", "tool_error", name(), "", workspace_root_.string(), 0, -1,
                        false, false, false, e.what()});
        return {false, e.what()};
    }
}
