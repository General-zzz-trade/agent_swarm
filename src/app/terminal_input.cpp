#include "terminal_input.h"

#include <algorithm>
#include <cstring>

#ifdef _WIN32
#include <io.h>
#include <conio.h>
#else
#include <unistd.h>
#endif

// ============================================================================
// Construction / Destruction
// ============================================================================

TerminalInput::TerminalInput(int input_fd, std::ostream& output)
    : input_fd_(input_fd), output_(output) {
#ifdef _WIN32
    is_tty_ = ::_isatty(input_fd_) != 0;
#else
    is_tty_ = ::isatty(input_fd_) != 0;
    if (is_tty_) {
        ::tcgetattr(input_fd_, &original_termios_);
    }
#endif
}

TerminalInput::~TerminalInput() {
    if (raw_mode_active_) {
        disable_raw_mode();
    }
}

// ============================================================================
// Public API
// ============================================================================

std::optional<std::string> TerminalInput::read_line(const std::string& prompt) {
    if (!is_tty_) {
        // Fallback for non-TTY (pipes, tests)
        std::string line;
        output_ << prompt << std::flush;
        if (!std::getline(std::cin, line)) {
            return std::nullopt;
        }
        return line;
    }

    buffer_.clear();
    cursor_pos_ = 0;
    history_index_ = -1;
    saved_current_.clear();
    cancelled_ = false;

    std::string active_prompt = prompt;

    enable_raw_mode();
    refresh_line(active_prompt);

    while (true) {
        int key = read_key();
        switch (key) {
            case KEY_ENTER: {
                // Multi-line: if buffer ends with backslash, continue on next line
                if (!buffer_.empty() && buffer_.back() == '\\') {
                    buffer_.pop_back();
                    buffer_ += '\n';
                    cursor_pos_ = buffer_.size();
                    active_prompt = "... ";
                    output_ << "\r\n";
                    refresh_line(active_prompt);
                    break;
                }
                disable_raw_mode();
                output_ << "\r\n";
                if (!buffer_.empty()) {
                    add_history(buffer_);
                }
                return buffer_;
            }
            case KEY_CTRL_D:
                if (buffer_.empty()) {
                    disable_raw_mode();
                    output_ << "\r\n";
                    return std::nullopt;
                }
                delete_char_at_cursor();
                refresh_line(active_prompt);
                break;
            case KEY_CTRL_C:
                disable_raw_mode();
                output_ << "^C\r\n";
                cancelled_ = true;
                return "";
            case KEY_TAB:
                attempt_completion();
                refresh_line(active_prompt);
                break;
            case KEY_BACKSPACE:
                delete_char_before_cursor();
                refresh_line(active_prompt);
                break;
            case KEY_DELETE:
                delete_char_at_cursor();
                refresh_line(active_prompt);
                break;
            case KEY_LEFT:
                move_cursor_left();
                refresh_line(active_prompt);
                break;
            case KEY_RIGHT:
                move_cursor_right();
                refresh_line(active_prompt);
                break;
            case KEY_UP:
                history_prev();
                refresh_line(active_prompt);
                break;
            case KEY_DOWN:
                history_next();
                refresh_line(active_prompt);
                break;
            case KEY_HOME:
            case KEY_CTRL_A:
                move_to_start();
                refresh_line(active_prompt);
                break;
            case KEY_END:
            case KEY_CTRL_E:
                move_to_end();
                refresh_line(active_prompt);
                break;
            case KEY_CTRL_U:
                clear_input_line();
                refresh_line(active_prompt);
                break;
            case KEY_CTRL_W:
                delete_word_back();
                refresh_line(active_prompt);
                break;
            case KEY_CTRL_K:
                kill_to_end();
                refresh_line(active_prompt);
                break;
            case KEY_CTRL_L:
                clear_screen();
                refresh_line(active_prompt);
                break;
            default:
                if (key >= 32 && key < 127) {
                    insert_char(static_cast<char>(key));
                    refresh_line(active_prompt);
                }
                break;
        }
    }
}

char TerminalInput::read_single_key() {
    if (!is_tty_) {
        char c = 0;
        std::cin.get(c);
        return c;
    }
    enable_raw_mode();
    char c = 0;
#ifdef _WIN32
    c = static_cast<char>(::_getch());
#else
    ::read(input_fd_, &c, 1);
#endif
    disable_raw_mode();
    return c;
}

void TerminalInput::set_slash_commands(const std::vector<std::string>& commands) {
    slash_commands_ = commands;
}

void TerminalInput::set_workspace_root(const std::filesystem::path& root) {
    workspace_root_ = root;
}

void TerminalInput::add_history(const std::string& line) {
    // Don't add duplicates of the most recent entry
    if (!history_.empty() && history_.back() == line) {
        return;
    }
    history_.push_back(line);
    if (static_cast<int>(history_.size()) > MAX_HISTORY) {
        history_.erase(history_.begin());
    }
}

bool TerminalInput::cancelled() const {
    return cancelled_;
}

void TerminalInput::reset_cancelled() {
    cancelled_ = false;
}

bool TerminalInput::is_tty() const {
    return is_tty_;
}

// ============================================================================
// Raw Mode
// ============================================================================

void TerminalInput::enable_raw_mode() {
    if (!is_tty_ || raw_mode_active_) return;
#ifndef _WIN32
    termios raw = original_termios_;
    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    // Leave c_oflag alone so output processing (e.g. \n -> \r\n) stays intact
    raw.c_cflag |= CS8;
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;
    ::tcsetattr(input_fd_, TCSAFLUSH, &raw);
#endif
    raw_mode_active_ = true;
}

void TerminalInput::disable_raw_mode() {
    if (!is_tty_ || !raw_mode_active_) return;
#ifndef _WIN32
    ::tcsetattr(input_fd_, TCSAFLUSH, &original_termios_);
#endif
    raw_mode_active_ = false;
}

// ============================================================================
// Key Reading
// ============================================================================

int TerminalInput::read_key() {
#ifdef _WIN32
    int c = ::_getch();
    if (c == EOF) return KEY_CTRL_D;
    if (c == 0 || c == 0xE0) {
        // Extended key — read second byte
        int ext = ::_getch();
        switch (ext) {
            case 72: return KEY_UP;
            case 80: return KEY_DOWN;
            case 75: return KEY_LEFT;
            case 77: return KEY_RIGHT;
            case 71: return KEY_HOME;
            case 79: return KEY_END;
            case 83: return KEY_DELETE;
        }
        return KEY_ESCAPE;
    }
    return c;
#else
    char c;
    int nread = ::read(input_fd_, &c, 1);
    if (nread <= 0) return KEY_CTRL_D;  // EOF
    if (c == KEY_ESCAPE) return read_escape_sequence();
    return static_cast<unsigned char>(c);
#endif
}

int TerminalInput::read_escape_sequence() {
#ifdef _WIN32
    return KEY_ESCAPE;
#else
    char seq[3];
    if (::read(input_fd_, &seq[0], 1) != 1) return KEY_ESCAPE;
    if (::read(input_fd_, &seq[1], 1) != 1) return KEY_ESCAPE;
    if (seq[0] == '[') {
        if (seq[1] >= '0' && seq[1] <= '9') {
            if (::read(input_fd_, &seq[2], 1) != 1) return KEY_ESCAPE;
            if (seq[2] == '~') {
                switch (seq[1]) {
                    case '1': return KEY_HOME;
                    case '3': return KEY_DELETE;
                    case '4': return KEY_END;
                    case '7': return KEY_HOME;
                    case '8': return KEY_END;
                }
            }
        } else {
            switch (seq[1]) {
                case 'A': return KEY_UP;
                case 'B': return KEY_DOWN;
                case 'C': return KEY_RIGHT;
                case 'D': return KEY_LEFT;
                case 'H': return KEY_HOME;
                case 'F': return KEY_END;
            }
        }
    } else if (seq[0] == 'O') {
        switch (seq[1]) {
            case 'H': return KEY_HOME;
            case 'F': return KEY_END;
        }
    }
    return KEY_ESCAPE;
#endif
}

// ============================================================================
// Editing Operations
// ============================================================================

void TerminalInput::insert_char(char c) {
    buffer_.insert(buffer_.begin() + static_cast<std::ptrdiff_t>(cursor_pos_), c);
    cursor_pos_++;
}

void TerminalInput::delete_char_before_cursor() {
    if (cursor_pos_ > 0) {
        buffer_.erase(cursor_pos_ - 1, 1);
        cursor_pos_--;
    }
}

void TerminalInput::delete_char_at_cursor() {
    if (cursor_pos_ < buffer_.size()) {
        buffer_.erase(cursor_pos_, 1);
    }
}

void TerminalInput::move_cursor_left() {
    if (cursor_pos_ > 0) {
        cursor_pos_--;
    }
}

void TerminalInput::move_cursor_right() {
    if (cursor_pos_ < buffer_.size()) {
        cursor_pos_++;
    }
}

void TerminalInput::move_to_start() {
    cursor_pos_ = 0;
}

void TerminalInput::move_to_end() {
    cursor_pos_ = buffer_.size();
}

void TerminalInput::delete_word_back() {
    if (cursor_pos_ == 0) return;
    // Skip trailing spaces
    std::size_t pos = cursor_pos_;
    while (pos > 0 && buffer_[pos - 1] == ' ') {
        pos--;
    }
    // Delete until next space or start
    while (pos > 0 && buffer_[pos - 1] != ' ') {
        pos--;
    }
    buffer_.erase(pos, cursor_pos_ - pos);
    cursor_pos_ = pos;
}

void TerminalInput::kill_to_end() {
    buffer_.erase(cursor_pos_);
}

void TerminalInput::clear_input_line() {
    buffer_.clear();
    cursor_pos_ = 0;
}

void TerminalInput::clear_screen() {
    output_ << "\033[2J\033[H" << std::flush;
}

// ============================================================================
// History
// ============================================================================

void TerminalInput::history_prev() {
    if (history_.empty()) return;
    if (history_index_ == -1) {
        // Save current buffer before navigating history
        saved_current_ = buffer_;
        history_index_ = static_cast<int>(history_.size()) - 1;
    } else if (history_index_ > 0) {
        history_index_--;
    } else {
        return;  // Already at oldest entry
    }
    buffer_ = history_[static_cast<std::size_t>(history_index_)];
    cursor_pos_ = buffer_.size();
}

void TerminalInput::history_next() {
    if (history_index_ == -1) return;  // Not navigating history
    if (history_index_ < static_cast<int>(history_.size()) - 1) {
        history_index_++;
        buffer_ = history_[static_cast<std::size_t>(history_index_)];
    } else {
        // Back to current input
        history_index_ = -1;
        buffer_ = saved_current_;
    }
    cursor_pos_ = buffer_.size();
}

// ============================================================================
// Tab Completion
// ============================================================================

void TerminalInput::attempt_completion() {
    auto completions = get_completions();
    if (completions.empty()) return;

    if (completions.size() == 1) {
        // Single match - insert it directly
        const auto& match = completions[0];

        // Find the token being completed to know how much to replace
        if (!buffer_.empty() && buffer_[0] == '/') {
            // Slash command completion: replace entire buffer
            buffer_ = match;
            // Add trailing space for convenience
            if (buffer_.back() != ' ') {
                buffer_ += ' ';
            }
        } else {
            // File path completion: find the @ token
            auto at_pos = buffer_.rfind('@', cursor_pos_ > 0 ? cursor_pos_ - 1 : 0);
            if (at_pos != std::string::npos) {
                buffer_ = buffer_.substr(0, at_pos + 1) + match;
                // Add trailing space if it's a file (not a directory)
                if (!match.empty() && match.back() != '/') {
                    buffer_ += ' ';
                }
            }
        }
        cursor_pos_ = buffer_.size();
    } else {
        // Multiple matches - insert common prefix and show candidates
        std::string prefix = common_prefix(completions);

        if (!buffer_.empty() && buffer_[0] == '/') {
            if (prefix.size() > buffer_.size()) {
                buffer_ = prefix;
                cursor_pos_ = buffer_.size();
            }
        } else {
            auto at_pos = buffer_.rfind('@', cursor_pos_ > 0 ? cursor_pos_ - 1 : 0);
            if (at_pos != std::string::npos) {
                std::string current_partial = buffer_.substr(at_pos + 1, cursor_pos_ - at_pos - 1);
                if (prefix.size() > current_partial.size()) {
                    buffer_ = buffer_.substr(0, at_pos + 1) + prefix;
                    cursor_pos_ = buffer_.size();
                }
            }
        }
        show_completions(completions);
    }
}

std::vector<std::string> TerminalInput::get_completions() {
    // Slash command completion: buffer starts with /
    if (!buffer_.empty() && buffer_[0] == '/') {
        return complete_slash(buffer_);
    }

    // File path completion: look for @ before cursor
    if (cursor_pos_ > 0) {
        auto at_pos = buffer_.rfind('@', cursor_pos_ - 1);
        if (at_pos != std::string::npos) {
            // Check that @ is at start or preceded by whitespace
            if (at_pos == 0 || buffer_[at_pos - 1] == ' ') {
                std::string partial = buffer_.substr(at_pos + 1, cursor_pos_ - at_pos - 1);
                return complete_file_path(partial);
            }
        }
    }

    return {};
}

std::vector<std::string> TerminalInput::complete_slash(const std::string& prefix) const {
    std::vector<std::string> matches;
    for (const auto& cmd : slash_commands_) {
        if (cmd.size() >= prefix.size() &&
            cmd.compare(0, prefix.size(), prefix) == 0) {
            matches.push_back(cmd);
        }
    }
    std::sort(matches.begin(), matches.end());
    return matches;
}

std::vector<std::string> TerminalInput::complete_file_path(const std::string& partial) const {
    std::vector<std::string> matches;
    if (workspace_root_.empty()) return matches;

    namespace fs = std::filesystem;

    // Split partial into directory part and filename prefix
    std::string dir_part;
    std::string name_prefix;
    auto last_sep = partial.rfind('/');
    if (last_sep != std::string::npos) {
        dir_part = partial.substr(0, last_sep + 1);
        name_prefix = partial.substr(last_sep + 1);
    } else {
        name_prefix = partial;
    }

    fs::path search_dir = workspace_root_ / dir_part;

    std::error_code ec;
    if (!fs::is_directory(search_dir, ec)) return matches;

    static const int MAX_COMPLETIONS = 20;
    int count = 0;

    for (auto it = fs::directory_iterator(search_dir, ec);
         it != fs::directory_iterator() && count < MAX_COMPLETIONS; ++it) {
        if (ec) break;

        std::string name = it->path().filename().string();

        // Skip hidden files unless the user typed a dot prefix
        if (name[0] == '.' && (name_prefix.empty() || name_prefix[0] != '.')) {
            continue;
        }

        if (name_prefix.empty() ||
            (name.size() >= name_prefix.size() &&
             name.compare(0, name_prefix.size(), name_prefix) == 0)) {
            std::string entry = dir_part + name;
            if (it->is_directory(ec)) {
                entry += '/';
            }
            matches.push_back(entry);
            count++;
        }
    }

    std::sort(matches.begin(), matches.end());
    return matches;
}

void TerminalInput::show_completions(const std::vector<std::string>& completions) {
    output_ << "\r\n";
    int col = 0;
    for (const auto& c : completions) {
        output_ << c;
        col += static_cast<int>(c.size());
        // Simple column layout: pad to 30 chars, max 3 columns
        int pad = 30 - static_cast<int>(c.size() % 30);
        if (pad <= 0) pad = 2;
        if (col + pad + 30 > 90) {
            output_ << "\r\n";
            col = 0;
        } else {
            for (int i = 0; i < pad; i++) output_ << ' ';
            col += pad;
        }
    }
    if (col > 0) output_ << "\r\n";
}

std::string TerminalInput::common_prefix(const std::vector<std::string>& strings) const {
    if (strings.empty()) return "";
    std::string prefix = strings[0];
    for (std::size_t i = 1; i < strings.size(); i++) {
        std::size_t len = std::min(prefix.size(), strings[i].size());
        std::size_t j = 0;
        while (j < len && prefix[j] == strings[i][j]) {
            j++;
        }
        prefix = prefix.substr(0, j);
        if (prefix.empty()) break;
    }
    return prefix;
}

// ============================================================================
// Display
// ============================================================================

void TerminalInput::refresh_line(const std::string& prompt) {
    // Move cursor to column 0, clear line, write prompt + buffer, position cursor
    output_ << "\r\033[K" << prompt << buffer_;
    // Move cursor back to correct position if not at end
    auto back = buffer_.size() - cursor_pos_;
    if (back > 0) {
        output_ << "\033[" << back << "D";
    }
    output_ << std::flush;
}
