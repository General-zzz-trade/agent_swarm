#include "terminal_ui_config.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <string>

namespace {

std::string normalize_env_value(const char* value) {
    if (value == nullptr) return "";

    std::string normalized = value;
    normalized.erase(std::remove_if(normalized.begin(), normalized.end(),
                    [](unsigned char ch) { return std::isspace(ch) != 0; }),
                    normalized.end());
    std::transform(normalized.begin(), normalized.end(), normalized.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return normalized;
}

bool parse_env_flag(const char* name, bool default_value) {
    const std::string value = normalize_env_value(std::getenv(name));
    if (value.empty()) return default_value;

    if (value == "1" || value == "true" || value == "yes" || value == "on") {
        return true;
    }
    if (value == "0" || value == "false" || value == "no" || value == "off") {
        return false;
    }
    return default_value;
}

}  // namespace

TerminalUiConfig load_terminal_ui_config() {
    TerminalUiConfig config;
    config.transient_ui = parse_env_flag("BOLT_TRANSIENT_UI", false);
    config.spinner_enabled = parse_env_flag("BOLT_SPINNER", config.transient_ui);
    config.overlay_status_bar =
        parse_env_flag("BOLT_OVERLAY_STATUS_BAR", config.transient_ui);
    return config;
}
