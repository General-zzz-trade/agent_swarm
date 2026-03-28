#include "edit_file_tool.h"

#include <algorithm>
#include <filesystem>
#include <sstream>
#include <stdexcept>
#include <vector>

#include "workspace_utils.h"

namespace {

enum class EditOperationType {
    exact_replace,
    line_replace,
};

struct EditOperation {
    EditOperationType type;
    std::string old_text;
    std::string new_text;
    std::size_t start_line = 0;
    std::size_t end_line = 0;
};

struct EditFileRequest {
    std::string path;
    EditOperationType mode;
    std::vector<EditOperation> operations;
};

struct AppliedReplacement {
    std::size_t index;
    std::string label;
    std::string old_text;
    std::string new_text;
    std::string before_preview;
    std::string after_preview;
};

struct EditAnalysis {
    std::filesystem::path target;
    std::string updated_content;
    std::vector<AppliedReplacement> applied_replacements;
    std::string mode_label;
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

bool starts_with_at(const std::string& value, std::size_t position, const std::string& prefix) {
    return position <= value.size() && value.compare(position, prefix.size(), prefix) == 0;
}

std::string extract_block(const std::string& args,
                          const std::string& start_marker,
                          const std::string& end_marker,
                          std::size_t search_from,
                          std::size_t* next_position) {
    const std::size_t block_start = args.find(start_marker, search_from);
    if (block_start == std::string::npos) {
        throw std::runtime_error("Missing block marker: " + start_marker);
    }

    std::size_t content_start = block_start + start_marker.size();
    if (content_start < args.size() && args[content_start] == '\n') {
        ++content_start;
    }

    const std::size_t block_end = args.find(end_marker, content_start);
    if (block_end == std::string::npos) {
        throw std::runtime_error("Missing block terminator: " + end_marker);
    }

    std::string value = args.substr(content_start, block_end - content_start);
    if (!value.empty() && value.back() == '\n') {
        value.pop_back();
    }

    if (next_position != nullptr) {
        *next_position = block_end + end_marker.size();
    }
    return value;
}

std::size_t skip_whitespace(const std::string& value, std::size_t position) {
    while (position < value.size() &&
           (value[position] == ' ' || value[position] == '\t' ||
            value[position] == '\r' || value[position] == '\n')) {
        ++position;
    }
    return position;
}

std::size_t first_operation_marker(const std::string& args) {
    const std::size_t old_marker = args.find("old<<<");
    const std::size_t replace_equal = args.find("replace_lines=");
    const std::size_t replace_colon = args.find("replace_lines:");

    std::size_t marker = std::string::npos;
    for (const std::size_t candidate : {old_marker, replace_equal, replace_colon}) {
        if (candidate == std::string::npos) {
            continue;
        }
        if (marker == std::string::npos || candidate < marker) {
            marker = candidate;
        }
    }
    return marker;
}

void parse_line_range_header(const std::string& header,
                             std::size_t* start_line,
                             std::size_t* end_line) {
    const std::string equal_prefix = "replace_lines=";
    const std::string colon_prefix = "replace_lines:";

    std::string value;
    if (header.rfind(equal_prefix, 0) == 0) {
        value = trim_copy(header.substr(equal_prefix.size()));
    } else if (header.rfind(colon_prefix, 0) == 0) {
        value = trim_copy(header.substr(colon_prefix.size()));
    } else {
        throw std::runtime_error("Invalid replace_lines header");
    }

    if (value.empty()) {
        throw std::runtime_error("replace_lines value is empty");
    }

    const std::size_t dash = value.find('-');
    std::string start_text = value;
    std::string end_text = value;
    if (dash != std::string::npos) {
        start_text = trim_copy(value.substr(0, dash));
        end_text = trim_copy(value.substr(dash + 1));
    }

    try {
        *start_line = static_cast<std::size_t>(std::stoull(start_text));
        *end_line = static_cast<std::size_t>(std::stoull(end_text));
    } catch (const std::exception&) {
        throw std::runtime_error("replace_lines must contain valid positive integers");
    }

    if (*start_line == 0 || *end_line == 0) {
        throw std::runtime_error("replace_lines must be 1-based");
    }
    if (*end_line < *start_line) {
        throw std::runtime_error("replace_lines end must be greater than or equal to start");
    }
}

EditFileRequest parse_edit_request(const std::string& raw_args) {
    std::string args = normalize_newlines(raw_args);
    if (args.find("old<<<") == std::string::npos &&
        args.find("replace_lines=") == std::string::npos &&
        args.find("replace_lines:") == std::string::npos &&
        args.find("\\n") != std::string::npos) {
        args = decode_escaped_newlines(args);
    }

    const std::size_t marker = first_operation_marker(args);
    if (marker == std::string::npos) {
        throw std::runtime_error(
            "edit_file args must contain either old<<< blocks or replace_lines headers");
    }

    const std::string path_part = trim_copy(args.substr(0, marker));
    std::string path;
    if (path_part.rfind("path=", 0) == 0) {
        path = trim_copy(path_part.substr(5));
    } else if (path_part.rfind("path:", 0) == 0) {
        path = trim_copy(path_part.substr(5));
    } else {
        throw std::runtime_error("edit_file args must start with 'path=' or 'path:'");
    }

    if (path.empty()) {
        throw std::runtime_error("edit_file path is empty");
    }

    std::vector<EditOperation> operations;
    EditOperationType mode = EditOperationType::exact_replace;
    bool mode_set = false;

    std::size_t position = marker;
    while (true) {
        position = skip_whitespace(args, position);
        if (position >= args.size()) {
            break;
        }

        if (starts_with_at(args, position, "old<<<")) {
            if (mode_set && mode != EditOperationType::exact_replace) {
                throw std::runtime_error("edit_file cannot mix exact replacements and line patches");
            }
            mode = EditOperationType::exact_replace;
            mode_set = true;

            EditOperation operation;
            operation.type = EditOperationType::exact_replace;
            operation.old_text = extract_block(args, "old<<<", ">>>", position, &position);
            if (operation.old_text.empty()) {
                throw std::runtime_error("edit_file old<<< block must not be empty");
            }

            position = skip_whitespace(args, position);
            if (!starts_with_at(args, position, "new<<<")) {
                throw std::runtime_error("edit_file expected new<<< block after old<<<");
            }
            operation.new_text = extract_block(args, "new<<<", ">>>", position, &position);
            operations.push_back(std::move(operation));
            continue;
        }

        if (starts_with_at(args, position, "replace_lines=") ||
            starts_with_at(args, position, "replace_lines:")) {
            if (mode_set && mode != EditOperationType::line_replace) {
                throw std::runtime_error("edit_file cannot mix exact replacements and line patches");
            }
            mode = EditOperationType::line_replace;
            mode_set = true;

            const std::size_t content_marker = args.find("content<<<", position);
            if (content_marker == std::string::npos) {
                throw std::runtime_error("edit_file line patch is missing content<<< block");
            }

            const std::string header = trim_copy(args.substr(position, content_marker - position));
            EditOperation operation;
            operation.type = EditOperationType::line_replace;
            parse_line_range_header(header, &operation.start_line, &operation.end_line);
            operation.new_text =
                extract_block(args, "content<<<", ">>>", content_marker, &position);
            operations.push_back(std::move(operation));
            continue;
        }

        throw std::runtime_error("Unsupported edit_file block; expected old<<< or replace_lines=");
    }

    if (operations.empty()) {
        throw std::runtime_error("edit_file requires at least one edit operation");
    }
    if (operations.size() > 20) {
        throw std::runtime_error("edit_file supports at most 20 edit operations");
    }

    return {path, mode, std::move(operations)};
}

std::size_t count_occurrences(const std::string& haystack, const std::string& needle) {
    if (needle.empty()) {
        return 0;
    }

    std::size_t count = 0;
    std::size_t position = 0;
    while ((position = haystack.find(needle, position)) != std::string::npos) {
        ++count;
        position += needle.size();
    }
    return count;
}

std::string preview_replacement(const std::string& content,
                                std::size_t replacement_offset,
                                std::size_t replacement_length) {
    constexpr std::size_t kContext = 80;
    const std::size_t start =
        replacement_offset > kContext ? replacement_offset - kContext : 0;
    const std::size_t end =
        std::min(content.size(), replacement_offset + replacement_length + kContext);

    std::string preview = content.substr(start, end - start);
    if (start > 0) {
        preview = "..." + preview;
    }
    if (end < content.size()) {
        preview += "...";
    }
    return preview;
}

std::string shorten_preview(const std::string& value, std::size_t max_length) {
    if (value.size() <= max_length) {
        return value;
    }
    return value.substr(0, max_length) + "...";
}

std::string detect_line_ending(const std::string& content) {
    return content.find("\r\n") != std::string::npos ? "\r\n" : "\n";
}

std::vector<std::string> split_lines_for_edit(const std::string& content, bool* trailing_newline) {
    std::string normalized = normalize_newlines(content);
    *trailing_newline = !normalized.empty() && normalized.back() == '\n';
    if (*trailing_newline) {
        normalized.pop_back();
    }

    std::vector<std::string> lines;
    if (normalized.empty()) {
        if (*trailing_newline) {
            lines.push_back("");
        }
        return lines;
    }

    std::stringstream input(normalized);
    std::string line;
    while (std::getline(input, line)) {
        lines.push_back(line);
    }
    return lines;
}

std::vector<std::string> split_replacement_lines(const std::string& content) {
    const std::string normalized = normalize_newlines(content);
    if (normalized.empty()) {
        return {};
    }

    std::vector<std::string> lines;
    std::stringstream input(normalized);
    std::string line;
    while (std::getline(input, line)) {
        lines.push_back(line);
    }
    return lines;
}

std::string join_lines_for_edit(const std::vector<std::string>& lines,
                                const std::string& line_ending,
                                bool trailing_newline) {
    if (lines.empty()) {
        return trailing_newline ? line_ending : "";
    }

    std::ostringstream output;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        if (i > 0) {
            output << line_ending;
        }
        output << lines[i];
    }
    if (trailing_newline) {
        output << line_ending;
    }
    return output.str();
}

std::string format_numbered_lines(std::size_t start_line, const std::vector<std::string>& lines) {
    if (lines.empty()) {
        return "[empty]";
    }

    std::ostringstream output;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        if (i > 0) {
            output << "\n";
        }
        output << (start_line + i) << ": " << lines[i];
    }
    return output.str();
}

EditAnalysis analyze_exact_replace_request(const EditFileRequest& request,
                                           const std::filesystem::path& workspace_root,
                                           const std::shared_ptr<IFileSystem>& file_system,
                                           const std::filesystem::path& target,
                                           const std::string& original_content) {
    std::string updated_content = original_content;
    std::vector<AppliedReplacement> applied_replacements;
    applied_replacements.reserve(request.operations.size());

    for (std::size_t i = 0; i < request.operations.size(); ++i) {
        const EditOperation& operation = request.operations[i];
        if (operation.old_text.size() > 100000 || operation.new_text.size() > 100000) {
            throw std::runtime_error("Refusing to edit blocks larger than 100000 bytes");
        }

        const std::size_t match_count = count_occurrences(updated_content, operation.old_text);
        if (match_count == 0) {
            throw std::runtime_error("Replacement " + std::to_string(i + 1) +
                                     " old text was not found in the file");
        }
        if (match_count > 1) {
            throw std::runtime_error("Replacement " + std::to_string(i + 1) +
                                     " matched multiple locations; each replacement must match exactly once");
        }

        const std::size_t match_offset = updated_content.find(operation.old_text);
        const std::string before_preview =
            preview_replacement(updated_content, match_offset, operation.old_text.size());
        updated_content.replace(match_offset, operation.old_text.size(), operation.new_text);
        const std::string after_preview =
            preview_replacement(updated_content, match_offset, operation.new_text.size());

        AppliedReplacement replacement;
        replacement.index = i + 1;
        replacement.label = "replacement " + std::to_string(i + 1);
        replacement.old_text = operation.old_text;
        replacement.new_text = operation.new_text;
        replacement.before_preview = before_preview;
        replacement.after_preview = after_preview;
        applied_replacements.push_back(std::move(replacement));
    }

    return {target, std::move(updated_content), std::move(applied_replacements),
            request.operations.size() == 1 ? "exact replacement" : "exact replacements"};
}

EditAnalysis analyze_line_replace_request(const EditFileRequest& request,
                                          const std::filesystem::path& workspace_root,
                                          const std::shared_ptr<IFileSystem>& file_system,
                                          const std::filesystem::path& target,
                                          const std::string& original_content) {
    const std::string line_ending = detect_line_ending(original_content);
    bool trailing_newline = false;
    const std::vector<std::string> original_lines =
        split_lines_for_edit(original_content, &trailing_newline);
    const std::size_t line_count = original_lines.size();

    if (line_count == 0) {
        throw std::runtime_error("Line patches require a file with at least one line");
    }

    for (std::size_t i = 0; i < request.operations.size(); ++i) {
        const EditOperation& operation = request.operations[i];
        if (operation.start_line > line_count || operation.end_line > line_count) {
            throw std::runtime_error("Replacement " + std::to_string(i + 1) +
                                     " references lines outside the file");
        }
    }

    std::vector<std::size_t> order(request.operations.size());
    for (std::size_t i = 0; i < order.size(); ++i) {
        order[i] = i;
    }

    std::sort(order.begin(), order.end(),
              [&request](std::size_t left, std::size_t right) {
                  const EditOperation& a = request.operations[left];
                  const EditOperation& b = request.operations[right];
                  if (a.start_line != b.start_line) {
                      return a.start_line < b.start_line;
                  }
                  return a.end_line < b.end_line;
              });

    for (std::size_t i = 1; i < order.size(); ++i) {
        const EditOperation& previous = request.operations[order[i - 1]];
        const EditOperation& current = request.operations[order[i]];
        if (current.start_line <= previous.end_line) {
            throw std::runtime_error("Line patches overlap; each patch must target a distinct line range");
        }
    }

    std::vector<std::string> current_lines = original_lines;
    std::vector<AppliedReplacement> applied_replacements(request.operations.size());

    std::sort(order.begin(), order.end(),
              [&request](std::size_t left, std::size_t right) {
                  return request.operations[left].start_line >
                         request.operations[right].start_line;
              });

    for (const std::size_t operation_index : order) {
        const EditOperation& operation = request.operations[operation_index];
        const std::size_t start_index = operation.start_line - 1;
        const std::size_t end_index = operation.end_line;

        const std::vector<std::string> before_lines(current_lines.begin() + start_index,
                                                    current_lines.begin() + end_index);
        const std::vector<std::string> replacement_lines =
            split_replacement_lines(operation.new_text);

        current_lines.erase(current_lines.begin() + start_index,
                            current_lines.begin() + end_index);
        current_lines.insert(current_lines.begin() + start_index,
                             replacement_lines.begin(),
                             replacement_lines.end());

        AppliedReplacement replacement;
        replacement.index = operation_index + 1;
        replacement.label = "lines " + std::to_string(operation.start_line) + "-" +
                            std::to_string(operation.end_line);
        replacement.old_text = format_numbered_lines(operation.start_line, before_lines);
        replacement.new_text = format_numbered_lines(operation.start_line, replacement_lines);
        replacement.before_preview = replacement.old_text;
        replacement.after_preview = replacement.new_text;
        applied_replacements[operation_index] = std::move(replacement);
    }

    const std::string updated_content =
        join_lines_for_edit(current_lines, line_ending, trailing_newline);
    return {target, updated_content, std::move(applied_replacements),
            request.operations.size() == 1 ? "line patch" : "line patches"};
}

EditAnalysis analyze_edit_request(const EditFileRequest& request,
                                  const std::filesystem::path& workspace_root,
                                  const std::shared_ptr<IFileSystem>& file_system) {
    const std::filesystem::path target =
        resolve_workspace_path(workspace_root, request.path);
    if (!is_within_workspace(target, workspace_root)) {
        throw std::runtime_error("Path is outside the workspace");
    }
    if (!file_system->exists(target)) {
        throw std::runtime_error("Target file does not exist");
    }
    if (!file_system->is_regular_file(target)) {
        throw std::runtime_error("Target path is not a regular file");
    }

    const FileReadResult read_result = file_system->read_text_file(target);
    if (!read_result.success) {
        throw std::runtime_error(read_result.error);
    }

    if (request.mode == EditOperationType::exact_replace) {
        return analyze_exact_replace_request(
            request, workspace_root, file_system, target, read_result.content);
    }
    return analyze_line_replace_request(
        request, workspace_root, file_system, target, read_result.content);
}

std::string format_replacement_details(const std::vector<AppliedReplacement>& replacements,
                                       std::size_t max_replacements) {
    std::ostringstream details;
    const std::size_t count = std::min(replacements.size(), max_replacements);
    for (std::size_t i = 0; i < count; ++i) {
        const AppliedReplacement& replacement = replacements[i];
        if (i > 0) {
            details << "\n";
        }
        details << replacement.label << ":\n";
        details << "- old:\n" << shorten_preview(replacement.old_text, 220) << "\n";
        details << "+ new:\n" << shorten_preview(replacement.new_text, 220) << "\n";
        details << "before:\n" << shorten_preview(replacement.before_preview, 260) << "\n";
        details << "after:\n" << shorten_preview(replacement.after_preview, 260);
    }
    if (replacements.size() > max_replacements) {
        details << "\n\n[truncated]";
    }
    return details.str();
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

EditFileTool::EditFileTool(std::filesystem::path workspace_root,
                           std::shared_ptr<IFileSystem> file_system,
                           std::shared_ptr<IAuditLogger> audit_logger)
    : workspace_root_(std::filesystem::weakly_canonical(std::move(workspace_root))),
      file_system_(std::move(file_system)),
      audit_logger_(std::move(audit_logger)) {
    if (file_system_ == nullptr) {
        throw std::invalid_argument("EditFileTool requires a file system");
    }
}

std::string EditFileTool::name() const {
    return "edit_file";
}

std::string EditFileTool::description() const {
    return "Replace one or more exact text blocks, or patch one or more line ranges, inside a workspace file.";
}

ToolPreview EditFileTool::preview(const std::string& args) const {
    try {
        const EditFileRequest request = parse_edit_request(args);
        const EditAnalysis analysis =
            analyze_edit_request(request, workspace_root_, file_system_);

        ToolPreview preview;
        preview.summary = "Edit file " +
                          workspace_relative_path(workspace_root_, analysis.target) +
                          " (" + std::to_string(analysis.applied_replacements.size()) + " " +
                          analysis.mode_label + ")";
        preview.details = "path: " +
                          workspace_relative_path(workspace_root_, analysis.target) + "\n" +
                          format_replacement_details(analysis.applied_replacements, 5);
        return preview;
    } catch (const std::exception& e) {
        return {"Unable to preview edit_file request", e.what()};
    }
}

ToolResult EditFileTool::run(const std::string& args) const {
    try {
        const EditFileRequest request = parse_edit_request(args);
        const EditAnalysis analysis =
            analyze_edit_request(request, workspace_root_, file_system_);
        const std::string relative_path =
            workspace_relative_path(workspace_root_, analysis.target);

        const FileWriteResult write_result =
            file_system_->write_text_file(analysis.target, analysis.updated_content);
        if (!write_result.success) {
            safe_log_audit(audit_logger_,
                           {"file", "tool_error", name(), relative_path,
                            workspace_root_.string(), 0, -1, false, false, false,
                            write_result.error});
            return {false, write_result.error};
        }

        safe_log_audit(audit_logger_,
                       {"file", "executed", name(), relative_path, workspace_root_.string(),
                        0, -1, true, true, false,
                        "operations=" +
                            std::to_string(analysis.applied_replacements.size()) +
                            " mode=" + analysis.mode_label});

        return {true,
                "EDITED FILE: " + relative_path +
                    "\nApplied " + std::to_string(analysis.applied_replacements.size()) +
                    " " + analysis.mode_label + "." +
                    "\nPreview:\n" +
                    format_replacement_details(analysis.applied_replacements, 3)};
    } catch (const std::exception& e) {
        safe_log_audit(audit_logger_,
                       {"file", "tool_error", name(), "", workspace_root_.string(), 0, -1,
                        false, false, false, e.what()});
        return {false, e.what()};
    }
}
