#include "self_check_runner.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <unordered_set>

#include "../agent/agent.h"

namespace {

struct CapabilitySpec {
    const char* name;
    const char* label;
    bool self_checkable;
    const char* args;
};

constexpr std::array<CapabilitySpec, 15> kCapabilitySpecs = {{
    {"calculator", "计算器", true, "2 + 2"},
    {"list_dir", "列目录", true, "src"},
    {"read_file", "读文件", true, "src/main.cpp"},
    {"search_code", "搜代码", true, "ToolRegistry"},
    {"write_file", "写文件", false, ""},
    {"edit_file", "改文件", false, ""},
    {"run_command", "命令执行", false, ""},
    {"list_processes", "列进程", true, "explorer.exe"},
    {"list_windows", "列窗口", true, ""},
    {"open_app", "打开应用", false, ""},
    {"focus_window", "聚焦窗口", false, ""},
    {"wait_for_window", "等待窗口", false, ""},
    {"inspect_ui", "查看控件", true, "max_elements=10"},
    {"click_element", "点击控件", false, ""},
    {"type_text", "输入文本", false, ""},
}};

std::string now_local_timestamp() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t current = std::chrono::system_clock::to_time_t(now);
    std::tm local_time{};
#ifdef _WIN32
    localtime_s(&local_time, &current);
#else
    localtime_r(&current, &local_time);
#endif
    std::ostringstream output;
    output << std::put_time(&local_time, "%Y-%m-%d %H:%M:%S");
    return output.str();
}

std::string shorten_detail(const std::string& value) {
    constexpr std::size_t kMaxLength = 160;
    if (value.size() <= kMaxLength) {
        return value;
    }
    return value.substr(0, kMaxLength) + "...";
}

CapabilityState build_missing_state(const CapabilitySpec& spec) {
    CapabilityState state;
    state.name = spec.name;
    state.label = spec.label;
    state.level = "unavailable";
    state.detail = "当前构建没有注册这个能力";
    return state;
}

CapabilityState build_untested_state(const CapabilitySpec& spec, const std::string& checked_at) {
    CapabilityState state;
    state.name = spec.name;
    state.label = spec.label;
    state.implemented = true;
    state.ready = true;
    state.verified = false;
    state.level = "untested";
    state.detail = "已实现，但默认自检不会执行有副作用动作";
    state.last_checked_at = checked_at;
    return state;
}

}  // namespace

SelfCheckRunner::SelfCheckRunner(Agent& agent, std::filesystem::path workspace_root)
    : agent_(agent),
      workspace_root_(std::move(workspace_root)) {}

std::vector<CapabilityState> SelfCheckRunner::build_initial_snapshot() const {
    const std::vector<std::string> tool_names = agent_.available_tool_names();
    const std::unordered_set<std::string> available(tool_names.begin(), tool_names.end());

    std::vector<CapabilityState> states;
    states.reserve(kCapabilitySpecs.size());
    for (const CapabilitySpec& spec : kCapabilitySpecs) {
        if (available.find(spec.name) == available.end()) {
            states.push_back(build_missing_state(spec));
            continue;
        }

        CapabilityState state;
        state.name = spec.name;
        state.label = spec.label;
        state.implemented = true;
        state.ready = true;
        state.verified = false;
        state.level = "untested";
        state.detail = spec.self_checkable ? "可运行自检，但尚未执行"
                                           : "已实现，但默认自检不会执行有副作用动作";
        states.push_back(std::move(state));
    }
    return states;
}

std::vector<CapabilityState> SelfCheckRunner::run() const {
    last_checked_at_ = now_local_timestamp();
    const std::vector<std::string> tool_names = agent_.available_tool_names();
    const std::unordered_set<std::string> available(tool_names.begin(), tool_names.end());

    std::vector<CapabilityState> states;
    states.reserve(kCapabilitySpecs.size());
    for (const CapabilitySpec& spec : kCapabilitySpecs) {
        if (available.find(spec.name) == available.end()) {
            states.push_back(build_missing_state(spec));
            continue;
        }

        if (!spec.self_checkable) {
            states.push_back(build_untested_state(spec, last_checked_at_));
            continue;
        }

        CapabilityState state;
        state.name = spec.name;
        state.label = spec.label;
        state.implemented = true;
        state.ready = true;
        state.last_checked_at = last_checked_at_;

        const ToolResult result = agent_.run_diagnostic_tool(spec.name, spec.args);
        state.verified = result.success;
        state.level = result.success ? "ok" : "degraded";
        state.detail = shorten_detail(result.content);
        states.push_back(std::move(state));
    }

    return states;
}

std::string SelfCheckRunner::last_checked_at() const {
    return last_checked_at_;
}
