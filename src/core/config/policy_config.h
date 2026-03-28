#ifndef CORE_CONFIG_POLICY_CONFIG_H
#define CORE_CONFIG_POLICY_CONFIG_H

#include <string>
#include <unordered_set>

struct PolicyConfig {
    std::unordered_set<std::string> read_only_tools = {
        "calculator",
        "list_dir",
        "list_processes",
        "list_windows",
        "inspect_ui",
        "read_file",
        "search_code",
        "wait_for_window",
    };

    std::unordered_set<std::string> bounded_write_tools = {
        "write_file",
        "edit_file",
    };

    std::unordered_set<std::string> bounded_command_tools = {
        "run_command",
    };

    std::unordered_set<std::string> bounded_desktop_tools = {
        "open_app",
        "focus_window",
        "click_element",
        "type_text",
    };

    bool block_high_risk = true;
};

#endif
