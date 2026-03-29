#include "build_and_test_tool.h"

#include <fstream>
#include <regex>
#include <sstream>

#include "workspace_utils.h"

namespace {

void safe_log_audit(const std::shared_ptr<IAuditLogger>& logger, const AuditEvent& event) {
    if (logger) {
        try { logger->log(event); } catch (...) {}
    }
}

std::string truncate(const std::string& s, std::size_t max) {
    if (s.size() <= max) return s;
    return s.substr(0, max) + "\n... [truncated " + std::to_string(s.size() - max) + " bytes]";
}

}  // namespace

BuildAndTestTool::BuildAndTestTool(std::filesystem::path workspace_root,
                                   std::shared_ptr<ICommandRunner> command_runner,
                                   std::shared_ptr<IAuditLogger> audit_logger)
    : workspace_root_(std::filesystem::weakly_canonical(std::move(workspace_root))),
      command_runner_(std::move(command_runner)),
      audit_logger_(std::move(audit_logger)) {
    if (!command_runner_) {
        throw std::invalid_argument("BuildAndTestTool requires a command runner");
    }
}

std::string BuildAndTestTool::name() const { return "build_and_test"; }

std::string BuildAndTestTool::description() const {
    return "Build the project and run tests. Returns structured output with pass/fail status "
           "and any compiler errors or test failures. Use after editing code to verify changes.";
}

ToolPreview BuildAndTestTool::preview(const std::string& args) const {
    const auto bs = detect_build_system();
    return {"Build (" + bs.name + ") and run tests",
            "build: " + bs.build_command + "\ntest: " + bs.test_command};
}

ToolSchema BuildAndTestTool::schema() const {
    return {name(), description(), {
        {"mode", "string", "build | test | both (default: both)", false},
    }};
}

BuildAndTestTool::BuildSystem BuildAndTestTool::detect_build_system() const {
    namespace fs = std::filesystem;

    // CMake (check for CMakeLists.txt + build dir)
    if (fs::exists(workspace_root_ / "CMakeLists.txt")) {
        std::string build_dir = "build";
        if (fs::exists(workspace_root_ / "build_perf")) {
            build_dir = "build_perf";
        }

        // Try to find cmake — check common locations
        std::string cmake = "cmake";
        const std::vector<std::string> cmake_paths = {
            "C:/Users/11847/AppData/Local/Programs/CLion/bin/cmake/win/x64/bin/cmake.exe",
            "C:/Program Files/CMake/bin/cmake.exe",
            "cmake",
        };
        for (const auto& path : cmake_paths) {
            if (path == "cmake" || fs::exists(path)) {
                cmake = "\"" + path + "\"";
                break;
            }
        }

        const std::string test_exe = (workspace_root_ / build_dir / "kernel_tests.exe").string();
        return {"cmake",
                cmake + " --build " + build_dir + " -j8",
                "\"" + test_exe + "\""};
    }

    // Cargo (Rust)
    if (fs::exists(workspace_root_ / "Cargo.toml")) {
        return {"cargo", "cargo build", "cargo test"};
    }

    // package.json (Node.js)
    if (fs::exists(workspace_root_ / "package.json")) {
        return {"npm", "npm run build", "npm test"};
    }

    // Makefile
    if (fs::exists(workspace_root_ / "Makefile") || fs::exists(workspace_root_ / "makefile")) {
        return {"make", "make -j8", "make test"};
    }

    // Go
    if (fs::exists(workspace_root_ / "go.mod")) {
        return {"go", "go build ./...", "go test ./..."};
    }

    // Python
    if (fs::exists(workspace_root_ / "setup.py") || fs::exists(workspace_root_ / "pyproject.toml")) {
        return {"python", "python -m py_compile .", "python -m pytest"};
    }

    return {"unknown", "echo 'No build system detected'", "echo 'No test system detected'"};
}

std::string BuildAndTestTool::parse_errors(const std::string& output) const {
    std::ostringstream errors;
    int error_count = 0;

    // Parse GCC/Clang error format: file:line:col: error: message
    static const std::regex gcc_error(
        R"(([^\s:]+):(\d+):(\d+):\s*(error|fatal error):\s*(.+))",
        std::regex::optimize);

    std::sregex_iterator it(output.begin(), output.end(), gcc_error);
    std::sregex_iterator end;
    for (; it != end && error_count < 20; ++it, ++error_count) {
        errors << "  ERROR " << (error_count + 1) << ": "
               << (*it)[1] << ":" << (*it)[2] << " — " << (*it)[5] << "\n";
    }

    // Parse test failure patterns
    static const std::regex test_fail(
        R"(\[FAIL\]\s*(.+))",
        std::regex::optimize);

    std::sregex_iterator it2(output.begin(), output.end(), test_fail);
    for (; it2 != end && error_count < 30; ++it2, ++error_count) {
        errors << "  TEST FAIL: " << (*it2)[1] << "\n";
    }

    return errors.str();
}

ToolResult BuildAndTestTool::run(const std::string& args) const {
    const std::string mode = trim_copy(args).empty() ? "both" : trim_copy(args);
    const auto bs = detect_build_system();

    std::ostringstream result;
    result << "BUILD_SYSTEM: " << bs.name << "\n";

    bool build_ok = true;
    bool test_ok = true;

    // --- Build phase ---
    if (mode == "build" || mode == "both") {
        result << "\n=== BUILD ===\n";
        result << "COMMAND: " << bs.build_command << "\n";

        const auto build_result = command_runner_->run(
            bs.build_command, workspace_root_, 120000);  // 2 min build timeout

        result << "EXIT_CODE: " << build_result.exit_code << "\n";
        build_ok = build_result.success && build_result.exit_code == 0;
        result << "STATUS: " << (build_ok ? "PASS" : "FAIL") << "\n";

        if (!build_ok) {
            const std::string combined = build_result.stdout_output + build_result.stderr_output;
            const std::string errors = parse_errors(combined);
            if (!errors.empty()) {
                result << "\nERRORS:\n" << errors;
            }
            result << "\nOUTPUT:\n" << truncate(combined, 16000);
        }

        safe_log_audit(audit_logger_,
            {"command", "executed", name(), bs.build_command, workspace_root_.string(),
             120000, build_result.exit_code, true, build_ok, build_result.timed_out, ""});
    }

    // --- Test phase (only if build succeeded) ---
    if ((mode == "test" || mode == "both") && build_ok) {
        result << "\n=== TEST ===\n";
        result << "COMMAND: " << bs.test_command << "\n";

        const auto test_result = command_runner_->run(
            bs.test_command, workspace_root_, 120000);

        result << "EXIT_CODE: " << test_result.exit_code << "\n";
        test_ok = test_result.success && test_result.exit_code == 0;
        result << "STATUS: " << (test_ok ? "PASS" : "FAIL") << "\n";

        const std::string combined = test_result.stdout_output + test_result.stderr_output;
        if (!test_ok) {
            const std::string errors = parse_errors(combined);
            if (!errors.empty()) {
                result << "\nFAILURES:\n" << errors;
            }
        }
        result << "\nOUTPUT:\n" << truncate(combined, 16000);

        safe_log_audit(audit_logger_,
            {"command", "executed", name(), bs.test_command, workspace_root_.string(),
             120000, test_result.exit_code, true, test_ok, test_result.timed_out, ""});
    }

    // --- Summary ---
    result << "\n=== SUMMARY ===\n";
    const bool all_ok = build_ok && test_ok;
    result << "RESULT: " << (all_ok ? "ALL PASS" : "FAILURE") << "\n";
    if (!build_ok) result << "  Build failed — fix compilation errors first\n";
    if (build_ok && !test_ok) result << "  Build OK but tests failed — fix test failures\n";

    return {all_ok, result.str()};
}
