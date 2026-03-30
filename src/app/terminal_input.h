#pragma once
#include <filesystem>
#include <functional>
#include <iostream>
#include <optional>
#include <string>
#include <vector>
#ifndef _WIN32
#include <termios.h>
#endif

class TerminalInput {
public:
    TerminalInput(int input_fd, std::ostream& output);
    ~TerminalInput();

    // Main read function - returns line or nullopt on EOF (Ctrl+D)
    std::optional<std::string> read_line(const std::string& prompt);

    // Single key read (for approval prompts etc)
    char read_single_key();

    // Configuration
    void set_slash_commands(const std::vector<std::string>& commands);
    void set_workspace_root(const std::filesystem::path& root);
    void add_history(const std::string& line);

    // State
    bool cancelled() const;
    void reset_cancelled();
    bool is_tty() const;

private:
    int input_fd_;
    std::ostream& output_;
    bool is_tty_ = false;
#ifndef _WIN32
    termios original_termios_;
#endif
    bool raw_mode_active_ = false;

    // Line editing state
    std::string buffer_;
    std::size_t cursor_pos_ = 0;

    // History
    std::vector<std::string> history_;
    int history_index_ = -1;
    std::string saved_current_;
    static const int MAX_HISTORY = 500;

    // Tab completion
    std::vector<std::string> slash_commands_;
    std::filesystem::path workspace_root_;

    // Cancellation
    bool cancelled_ = false;

    // Raw mode
    void enable_raw_mode();
    void disable_raw_mode();

    // Key reading
    enum Key {
        KEY_ENTER = 13,
        KEY_TAB = 9,
        KEY_BACKSPACE = 127,
        KEY_CTRL_A = 1,
        KEY_CTRL_C = 3,
        KEY_CTRL_D = 4,
        KEY_CTRL_E = 5,
        KEY_CTRL_K = 11,
        KEY_CTRL_L = 12,
        KEY_CTRL_U = 21,
        KEY_CTRL_W = 23,
        KEY_ESCAPE = 27,
        KEY_UP = 1000,
        KEY_DOWN = 1001,
        KEY_RIGHT = 1002,
        KEY_LEFT = 1003,
        KEY_HOME = 1004,
        KEY_END = 1005,
        KEY_DELETE = 1006,
    };
    int read_key();
    int read_escape_sequence();

    // Editing operations
    void insert_char(char c);
    void delete_char_before_cursor();
    void delete_char_at_cursor();
    void move_cursor_left();
    void move_cursor_right();
    void move_to_start();
    void move_to_end();
    void delete_word_back();
    void kill_to_end();
    void clear_input_line();
    void clear_screen();

    // History
    void history_prev();
    void history_next();

    // Tab completion
    void attempt_completion();
    std::vector<std::string> get_completions();
    std::vector<std::string> complete_slash(const std::string& prefix) const;
    std::vector<std::string> complete_file_path(const std::string& partial) const;
    void show_completions(const std::vector<std::string>& completions);
    std::string common_prefix(const std::vector<std::string>& strings) const;

    // Display
    void refresh_line(const std::string& prompt);
};
