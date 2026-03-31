#include "build_and_test_tool.h"

#include <fstream>
#include <filesystem>
#include <regex>
#include <sstream>
#include <vector>

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

std::string shell_quote(const std::string& value) {
    if (value.find_first_of(" \t\"") == std::string::npos) {
        return value;
    }

    std::string escaped;
    escaped.reserve(value.size() + 8);
    escaped.push_back('"');
    for (char ch : value) {
        if (ch == '"') {
            escaped += "\\\"";
        } else {
            escaped.push_back(ch);
        }
    }
    escaped.push_back('"');
    return escaped;
}

std::string resolve_tool_binary(const std::vector<std::string>& candidates) {
    namespace fs = std::filesystem;

    for (const auto& candidate : candidates) {
        if (candidate == "cmake" || candidate == "ctest") {
            return candidate;
        }
        if (fs::exists(candidate)) {
            return shell_quote(candidate);
        }
    }
    return candidates.empty() ? "" : candidates.back();
}

std::string choose_cmake_build_dir(const std::filesystem::path& workspace_root) {
    namespace fs = std::filesystem;

    const std::vector<std::string> preferred_dirs = {
        "build", "build_perf", "cmake-build-debug", "cmake-build-release"
    };

    for (const auto& dir : preferred_dirs) {
        const fs::path build_dir = workspace_root / dir;
        if (fs::exists(build_dir / "CTestTestfile.cmake") ||
            fs::exists(build_dir / "CMakeCache.txt")) {
            return dir;
        }
    }

    for (const auto& dir : preferred_dirs) {
        if (fs::exists(workspace_root / dir)) {
            return dir;
        }
    }

    return "build";
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
            "build: " + bs.build_command + "\ntest: " +
                (bs.test_command.empty() ? "[none detected]" : bs.test_command)};
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
        const std::string build_dir = choose_cmake_build_dir(workspace_root_);
        const bool needs_configure =
            !fs::exists(workspace_root_ / build_dir / "CMakeCache.txt");

        const std::string cmake = resolve_tool_binary({
            "C:/Users/11847/AppData/Local/Programs/CLion/bin/cmake/win/x64/bin/cmake.exe",
            "C:/Program Files/CMake/bin/cmake.exe",
            "cmake",
        });
        const std::string ctest = resolve_tool_binary({
            "C:/Users/11847/AppData/Local/Programs/CLion/bin/cmake/win/x64/bin/ctest.exe",
            "C:/Program Files/CMake/bin/ctest.exe",
            "ctest",
        });

        std::string build_command;
        if (needs_configure) {
            build_command = cmake + " -B " + shell_quote(build_dir) + " -S . && " +
                            cmake + " --build " + shell_quote(build_dir) + " --parallel 8";
        } else {
            build_command = cmake + " --build " + shell_quote(build_dir) + " --parallel 8";
        }

        return {"cmake",
                build_command,
                ctest + " --test-dir " + shell_quote(build_dir) + " --output-on-failure"};
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
    const std::string raw_mode = trim_copy(args);
    const std::string mode = raw_mode.empty() || raw_mode == "auto" ? "both" : raw_mode;
    if (mode != "build" && mode != "test" && mode != "both") {
        return {false, "Invalid mode. Use: build, test, both, or auto"};
    }
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
        if (bs.test_command.empty()) {
            result << "COMMAND: [none]\nSTATUS: SKIP\n";
            test_ok = true;
        } else {
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
    }

    // --- Summary ---
    result << "\n=== SUMMARY ===\n";
    const bool all_ok = build_ok && test_ok;
    result << "RESULT: " << (all_ok ? "ALL PASS" : "FAILURE") << "\n";
    if (!build_ok) result << "  Build failed — fix compilation errors first\n";
    if (build_ok && !test_ok) result << "  Build OK but tests failed — fix test failures\n";

    return {all_ok, result.str()};
}
