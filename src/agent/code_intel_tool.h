#ifndef AGENT_CODE_INTEL_TOOL_H
#define AGENT_CODE_INTEL_TOOL_H

#include <filesystem>
#include <memory>
#include <string>

#include "../core/interfaces/file_system.h"
#include "tool.h"

/// Code intelligence tool: provides lightweight code understanding
/// without external dependencies (no tree-sitter needed).
///
/// Uses regex-based pattern matching to find:
/// - Function/method definitions ("find_def: functionName")
/// - Class/struct definitions ("find_class: ClassName")
/// - All references to a symbol ("find_refs: symbolName")
/// - Includes/imports for a file ("find_includes: filename.h")
/// - Function signatures in a file ("list_functions: path/to/file.cpp")
///
/// Supports: C, C++, Python, JavaScript/TypeScript, Rust, Go, Java
///
/// While not as precise as tree-sitter AST parsing, this covers
/// the most common code navigation tasks that make an agent effective.
class CodeIntelTool : public Tool {
public:
    CodeIntelTool(std::filesystem::path workspace_root,
                  std::shared_ptr<IFileSystem> file_system);

    std::string name() const override;
    std::string description() const override;
    ToolResult run(const std::string& args) const override;
    ToolSchema schema() const override;
    bool is_read_only() const override { return true; }

private:
    std::filesystem::path workspace_root_;
    std::shared_ptr<IFileSystem> file_system_;

    ToolResult find_definition(const std::string& symbol) const;
    ToolResult find_class(const std::string& class_name) const;
    ToolResult find_references(const std::string& symbol) const;
    ToolResult find_includes(const std::string& filename) const;
    ToolResult list_functions(const std::string& file_path) const;

    struct Match {
        std::string file;
        int line;
        std::string content;
    };

    std::vector<Match> search_pattern(const std::string& pattern, int max_results = 30) const;
    std::vector<Match> search_in_file(const std::filesystem::path& file,
                                       const std::string& pattern) const;
    void collect_source_files(const std::filesystem::path& dir,
                              std::vector<std::filesystem::path>& out) const;
};

#endif
