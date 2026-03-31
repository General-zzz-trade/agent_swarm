#pragma once

struct TerminalUiConfig {
    bool transient_ui = false;
    bool spinner_enabled = false;
    bool overlay_status_bar = false;
};

TerminalUiConfig load_terminal_ui_config();
