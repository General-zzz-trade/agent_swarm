#ifndef AGENT_BUILD_AND_TEST_TOOL_H
#define AGENT_BUILD_AND_TEST_TOOL_H

#include <filesystem>
#include <memory>

#include "../core/interfaces/audit_logger.h"
#include "../core/interfaces/command_runner.h"
#include "tool.h"

/// Build-and-test tool: compiles the project and runs tests in one step.
/// Parses compiler errors and test failures into structured output.
///
/// This enables the autonomous write→build→test→fix loop:
///   1. Agent edits code
///   2. Agent calls build_and_test
///   3. If errors: agent reads the structured error output and fixes code
///   4. Agent calls build_and_test again
///   5. Repeat until all tests pass
///
/// The tool auto-detects the build system (CMake, make, cargo, npm, etc.)
/// and runs the appropriate build + test commands.
class BuildAndTestTool : public Tool {
public:
    BuildAndTestTool(std::filesystem::path workspace_root,
                     std::shared_ptr<ICommandRunner> command_runner,
                     std::shared_ptr<IAuditLogger> audit_logger = nullptr);

    std::string name() const override;
    std::string description() const override;
    ToolPreview preview(const std::string& args) const override;
    ToolResult run(const std::string& args) const override;

    ToolSchema schema() const override;

private:
    std::filesystem::path workspace_root_;
    std::shared_ptr<ICommandRunner> command_runner_;
    std::shared_ptr<IAuditLogger> audit_logger_;

    struct BuildSystem {
        std::string name;
        std::string build_command;
        std::string test_command;
    };

    BuildSystem detect_build_system() const;
    std::string parse_errors(const std::string& output) const;
};

#endif
