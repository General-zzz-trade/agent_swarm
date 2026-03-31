#include "terminal_renderer.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

#ifdef _WIN32
#include <io.h>
#endif

#include "../core/interfaces/approval_provider.h"
#include "terminal_ui_config.h"

namespace {

// ANSI escape sequences
const char* const kReset      = "\033[0m";
const char* const kBoldOn     = "\033[1m";
const char* const kBoldOff    = "\033[22m";
const char* const kDimOn      = "\033[2m";
const char* const kDimOff     = "\033[22m";
const char* const kItalicOn   = "\033[3m";
const char* const kItalicOff  = "\033[23m";
const char* const kUnderOn    = "\033[4m";
const char* const kUnderOff   = "\033[24m";

const char* const kFgRed      = "\033[31m";
const char* const kFgGreen    = "\033[32m";
const char* const kFgYellow   = "\033[33m";
const char* const kFgBlue     = "\033[34m";
const char* const kFgMagenta  = "\033[35m";
const char* const kFgCyan     = "\033[36m";
const char* const kFgWhite    = "\033[97m";

const char* const kBgBlue     = "\033[44m";
const char* const kBgGray     = "\033[48;5;236m";

// Split a string by newlines
std::vector<std::string> split_lines(const std::string& text) {
    std::vector<std::string> lines;
    std::istringstream stream(text);
    std::string line;
    while (std::getline(stream, line)) {
        lines.push_back(line);
    }
    if (lines.empty()) {
        lines.push_back("");
    }
    return lines;
}

// Strip ANSI escape sequences for length calculation
std::size_t visible_length(const std::string& s) {
    std::size_t len = 0;
    bool in_escape = false;
    for (char ch : s) {
        if (in_escape) {
            if ((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z')) {
                in_escape = false;
            }
            continue;
        }
        if (ch == '\033') {
            in_escape = true;
            continue;
        }
        ++len;
    }
    return len;
}

// Pad string to width (accounting for ANSI codes)
std::string pad_to(const std::string& s, int width) {
    int vis = static_cast<int>(visible_length(s));
    if (vis >= width) return s;
    return s + std::string(width - vis, ' ');
}

// Trim whitespace
std::string trim(const std::string& s) {
    auto begin = s.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) return "";
    auto end = s.find_last_not_of(" \t\r\n");
    return s.substr(begin, end - begin + 1);
}

// Check if string starts with prefix
bool starts_with(const std::string& s, const std::string& prefix) {
    return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
}

// Set of C/C++/JS/Python keywords for highlighting
bool is_keyword(const std::string& word) {
    static const char* const keywords[] = {
        "if", "else", "for", "while", "return", "class", "struct",
        "void", "int", "string", "auto", "const", "let", "var",
        "function", "def", "import", "from", "try", "catch", "throw",
        "new", "delete", "true", "false", "null", "nullptr",
        "switch", "case", "break", "continue", "do", "enum",
        "namespace", "using", "template", "typename", "virtual",
        "override", "public", "private", "protected", "static",
        "inline", "extern", "typedef", "sizeof", "this", "self",
        "yield", "async", "await", "export", "default", "extends",
        "implements", "interface", "package", "final", "abstract",
    };
    for (const char* kw : keywords) {
        if (word == kw) return true;
    }
    return false;
}

bool is_preprocessor_directive(const std::string& trimmed_line) {
    return !trimmed_line.empty() && trimmed_line[0] == '#' &&
           (starts_with(trimmed_line, "#include") ||
            starts_with(trimmed_line, "#define") ||
            starts_with(trimmed_line, "#pragma") ||
            starts_with(trimmed_line, "#ifdef") ||
            starts_with(trimmed_line, "#ifndef") ||
            starts_with(trimmed_line, "#endif") ||
            starts_with(trimmed_line, "#if ") ||
            starts_with(trimmed_line, "#else") ||
            starts_with(trimmed_line, "#undef"));
}

bool is_alpha_or_underscore(char ch) {
    return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || ch == '_';
}

bool is_alnum_or_underscore(char ch) {
    return is_alpha_or_underscore(ch) || (ch >= '0' && ch <= '9');
}

// Parse @@ -a,b +c,d @@ to extract starting line number for new side
int parse_hunk_start(const std::string& hunk_header) {
    // Format: @@ -old_start[,old_count] +new_start[,new_count] @@
    auto plus_pos = hunk_header.find('+');
    if (plus_pos == std::string::npos) return 1;
    int line_num = 0;
    for (std::size_t i = plus_pos + 1; i < hunk_header.size(); ++i) {
        if (hunk_header[i] >= '0' && hunk_header[i] <= '9') {
            line_num = line_num * 10 + (hunk_header[i] - '0');
        } else {
            break;
        }
    }
    return line_num > 0 ? line_num : 1;
}

}  // namespace

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------

TerminalRenderer::TerminalRenderer(std::ostream& output)
    : output_(output) {
    // Detect color support
#ifdef _WIN32
    if (!_isatty(_fileno(stdout))) {
#else
    if (!isatty(STDOUT_FILENO)) {
#endif
        colors_enabled_ = false;
    } else {
        const char* term = std::getenv("TERM");
        if (term && std::strcmp(term, "dumb") == 0) {
            colors_enabled_ = false;
        }
    }
    // Also respect NO_COLOR convention
    if (std::getenv("NO_COLOR")) {
        colors_enabled_ = false;
    }

    const TerminalUiConfig ui_config = load_terminal_ui_config();
    overlay_status_bar_enabled_ = colors_enabled_ && ui_config.overlay_status_bar;

    update_terminal_width();
}

// ---------------------------------------------------------------------------
// Terminal dimensions
// ---------------------------------------------------------------------------

int TerminalRenderer::terminal_width() const {
    return terminal_width_;
}

void TerminalRenderer::update_terminal_width() {
#ifdef _WIN32
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi)) {
        int w = csbi.srWindow.Right - csbi.srWindow.Left + 1;
        int h = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
        if (w > 0) terminal_width_ = w;
        if (h > 0) terminal_height_ = h;
    }
#else
    struct winsize ws {};
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0) {
        terminal_width_ = ws.ws_col;
        terminal_height_ = ws.ws_row > 0 ? ws.ws_row : 24;
    }
#endif
}

// ---------------------------------------------------------------------------
// ANSI helpers
// ---------------------------------------------------------------------------

std::string TerminalRenderer::bold(const std::string& s) const {
    if (!colors_enabled_) return s;
    return std::string(kBoldOn) + s + kBoldOff;
}

std::string TerminalRenderer::dim(const std::string& s) const {
    if (!colors_enabled_) return s;
    return std::string(kDimOn) + s + kDimOff;
}

std::string TerminalRenderer::italic(const std::string& s) const {
    if (!colors_enabled_) return s;
    return std::string(kItalicOn) + s + kItalicOff;
}

std::string TerminalRenderer::color(const std::string& s, const std::string& code) const {
    if (!colors_enabled_) return s;
    return code + s + kReset;
}

std::string TerminalRenderer::green(const std::string& s) const {
    return color(s, kFgGreen);
}

std::string TerminalRenderer::red(const std::string& s) const {
    return color(s, kFgRed);
}

std::string TerminalRenderer::yellow(const std::string& s) const {
    return color(s, kFgYellow);
}

std::string TerminalRenderer::cyan(const std::string& s) const {
    return color(s, kFgCyan);
}

std::string TerminalRenderer::magenta(const std::string& s) const {
    return color(s, kFgMagenta);
}

std::string TerminalRenderer::bg_color(const std::string& s, const std::string& code) const {
    if (!colors_enabled_) return s;
    return code + s + kReset;
}

// ---------------------------------------------------------------------------
// Word wrap
// ---------------------------------------------------------------------------

std::string TerminalRenderer::word_wrap(const std::string& text, int width) const {
    if (width <= 0) return text;

    std::string result;
    result.reserve(text.size() + text.size() / width);

    int col = 0;
    std::size_t i = 0;
    while (i < text.size()) {
        // Find next word boundary
        if (text[i] == '\n') {
            result += '\n';
            col = 0;
            ++i;
            continue;
        }

        // Collect a word
        std::size_t word_start = i;
        while (i < text.size() && text[i] != ' ' && text[i] != '\n') {
            ++i;
        }
        std::string word = text.substr(word_start, i - word_start);
        int word_len = static_cast<int>(word.size());

        // Skip trailing spaces
        while (i < text.size() && text[i] == ' ') {
            ++i;
        }

        if (col > 0 && col + 1 + word_len > width) {
            result += '\n';
            col = 0;
        }

        if (col > 0) {
            result += ' ';
            ++col;
        }

        result += word;
        col += word_len;
    }

    return result;
}

// ---------------------------------------------------------------------------
// Markdown rendering
// ---------------------------------------------------------------------------

std::string TerminalRenderer::render_inline_markdown(const std::string& line) const {
    std::string result;
    result.reserve(line.size() * 2);

    std::size_t i = 0;
    while (i < line.size()) {
        // Bold: **text**
        if (i + 1 < line.size() && line[i] == '*' && line[i + 1] == '*') {
            std::size_t end = line.find("**", i + 2);
            if (end != std::string::npos) {
                std::string inner = line.substr(i + 2, end - i - 2);
                result += bold(inner);
                i = end + 2;
                continue;
            }
        }

        // Italic: *text* (but not **)
        if (line[i] == '*' && (i + 1 >= line.size() || line[i + 1] != '*')) {
            std::size_t end = line.find('*', i + 1);
            if (end != std::string::npos && end > i + 1) {
                std::string inner = line.substr(i + 1, end - i - 1);
                result += italic(inner);
                i = end + 1;
                continue;
            }
        }

        // Inline code: `code`
        if (line[i] == '`') {
            std::size_t end = line.find('`', i + 1);
            if (end != std::string::npos) {
                std::string code_text = line.substr(i + 1, end - i - 1);
                result += cyan(code_text);
                i = end + 1;
                continue;
            }
        }

        // Links: [text](url)
        if (line[i] == '[') {
            std::size_t bracket_end = line.find(']', i + 1);
            if (bracket_end != std::string::npos &&
                bracket_end + 1 < line.size() && line[bracket_end + 1] == '(') {
                std::size_t paren_end = line.find(')', bracket_end + 2);
                if (paren_end != std::string::npos) {
                    std::string link_text = line.substr(i + 1, bracket_end - i - 1);
                    std::string url = line.substr(bracket_end + 2, paren_end - bracket_end - 2);
                    if (colors_enabled_) {
                        result += std::string(kUnderOn) + link_text + kUnderOff;
                        result += " " + dim(url);
                    } else {
                        result += link_text + " (" + url + ")";
                    }
                    i = paren_end + 1;
                    continue;
                }
            }
        }

        result += line[i];
        ++i;
    }

    return result;
}

void TerminalRenderer::render_code_block(const std::string& code, const std::string& lang) {
    auto lines = split_lines(code);
    int gutter_width = static_cast<int>(std::to_string(lines.size()).size()) + 1;
    int content_width = terminal_width_ - gutter_width - 3;

    if (!lang.empty()) {
        output_ << dim("  " + lang) << "\n";
    }

    for (std::size_t i = 0; i < lines.size(); ++i) {
        std::string line_num = std::to_string(i + 1);
        std::string gutter = std::string(gutter_width - static_cast<int>(line_num.size()), ' ') + line_num;

        std::string highlighted = highlight_code_keywords(lines[i]);

        if (colors_enabled_) {
            output_ << dim(gutter) << " " << kBgGray << " " << highlighted << kReset << "\n";
        } else {
            output_ << gutter << " │ " << lines[i] << "\n";
        }
    }
}

std::string TerminalRenderer::highlight_code_keywords(const std::string& code) const {
    if (!colors_enabled_) return code;

    std::string trimmed = trim(code);

    // Preprocessor directives - color entire line
    if (is_preprocessor_directive(trimmed)) {
        return std::string(kFgMagenta) + code + kReset;
    }

    std::string result;
    result.reserve(code.size() * 2);

    std::size_t i = 0;
    while (i < code.size()) {
        // Single-line comments: // or #
        if (i + 1 < code.size() && code[i] == '/' && code[i + 1] == '/') {
            result += std::string(kDimOn) + code.substr(i) + kDimOff;
            break;
        }
        if (code[i] == '#' && (i == 0 || code[i - 1] == ' ' || code[i - 1] == '\t')) {
            // Only treat as comment if not a preprocessor (already handled above)
            // In code blocks for Python etc., # starts a comment
            result += std::string(kDimOn) + code.substr(i) + kDimOff;
            break;
        }

        // String literals (double quotes)
        if (code[i] == '"') {
            std::size_t end = i + 1;
            while (end < code.size()) {
                if (code[end] == '\\' && end + 1 < code.size()) {
                    end += 2;
                    continue;
                }
                if (code[end] == '"') {
                    ++end;
                    break;
                }
                ++end;
            }
            result += std::string(kFgGreen) + code.substr(i, end - i) + kReset;
            i = end;
            continue;
        }

        // String literals (single quotes)
        if (code[i] == '\'') {
            std::size_t end = i + 1;
            while (end < code.size()) {
                if (code[end] == '\\' && end + 1 < code.size()) {
                    end += 2;
                    continue;
                }
                if (code[end] == '\'') {
                    ++end;
                    break;
                }
                ++end;
            }
            result += std::string(kFgGreen) + code.substr(i, end - i) + kReset;
            i = end;
            continue;
        }

        // Numbers
        if (code[i] >= '0' && code[i] <= '9' &&
            (i == 0 || !is_alnum_or_underscore(code[i - 1]))) {
            std::size_t end = i;
            while (end < code.size() &&
                   ((code[end] >= '0' && code[end] <= '9') ||
                    code[end] == '.' || code[end] == 'x' || code[end] == 'X' ||
                    code[end] == 'f' || code[end] == 'L' || code[end] == 'u' ||
                    code[end] == 'U' ||
                    (code[end] >= 'a' && code[end] <= 'f') ||
                    (code[end] >= 'A' && code[end] <= 'F'))) {
                ++end;
            }
            result += std::string(kFgCyan) + code.substr(i, end - i) + kReset;
            i = end;
            continue;
        }

        // Identifiers / keywords
        if (is_alpha_or_underscore(code[i])) {
            std::size_t end = i;
            while (end < code.size() && is_alnum_or_underscore(code[end])) {
                ++end;
            }
            std::string word = code.substr(i, end - i);
            if (is_keyword(word)) {
                result += std::string(kFgYellow) + word + kReset;
            } else {
                result += word;
            }
            i = end;
            continue;
        }

        result += code[i];
        ++i;
    }

    return result;
}

void TerminalRenderer::render_markdown(const std::string& text) {
    auto lines = split_lines(text);
    bool in_code = false;
    std::string code_lang;
    std::string code_buffer;

    for (const auto& line : lines) {
        // Code block fences
        if (starts_with(trim(line), "```")) {
            if (!in_code) {
                in_code = true;
                std::string trimmed = trim(line);
                code_lang = trimmed.size() > 3 ? trimmed.substr(3) : "";
                code_buffer.clear();
            } else {
                render_code_block(code_buffer, code_lang);
                in_code = false;
                code_lang.clear();
                code_buffer.clear();
            }
            continue;
        }

        if (in_code) {
            if (!code_buffer.empty()) code_buffer += "\n";
            code_buffer += line;
            continue;
        }

        // Horizontal rule
        std::string trimmed = trim(line);
        if (trimmed == "---" || trimmed == "***" || trimmed == "___") {
            output_ << dim(std::string(terminal_width_, '\xe2') .size() > 0
                          ? std::string(terminal_width_, ' ')  // fallback
                          : std::string(terminal_width_, '-'));
            // Use proper unicode horizontal line
            std::string hr;
            for (int i = 0; i < terminal_width_; ++i) {
                hr += "\xe2\x94\x80";  // UTF-8 for ─
            }
            output_ << dim(hr) << "\n";
            continue;
        }

        // Headers
        if (starts_with(trimmed, "### ")) {
            output_ << bold(cyan(trimmed.substr(4))) << "\n";
            continue;
        }
        if (starts_with(trimmed, "## ")) {
            output_ << bold(cyan(trimmed.substr(3))) << "\n";
            continue;
        }
        if (starts_with(trimmed, "# ")) {
            output_ << bold(magenta(trimmed.substr(2))) << "\n";
            continue;
        }

        // Unordered list items
        if ((starts_with(trimmed, "- ") || starts_with(trimmed, "* ")) &&
            trimmed.size() > 2) {
            // Preserve leading whitespace for nested lists
            std::size_t indent = line.find_first_not_of(" \t");
            std::string prefix(indent, ' ');
            std::string item = trimmed.substr(2);
            output_ << prefix << "  \xe2\x80\xa2 " << render_inline_markdown(item) << "\n";
            continue;
        }

        // Ordered list items
        if (!trimmed.empty() && trimmed[0] >= '1' && trimmed[0] <= '9') {
            auto dot_pos = trimmed.find(". ");
            if (dot_pos != std::string::npos && dot_pos < 4) {
                std::string num = trimmed.substr(0, dot_pos + 1);
                std::string item = trimmed.substr(dot_pos + 2);
                std::size_t indent = line.find_first_not_of(" \t");
                std::string prefix(indent, ' ');
                output_ << prefix << "  " << num << " " << render_inline_markdown(item) << "\n";
                continue;
            }
        }

        // Blockquote
        if (starts_with(trimmed, "> ")) {
            std::string quote_text = trimmed.substr(2);
            if (colors_enabled_) {
                output_ << dim("  \xe2\x94\x82 ") << italic(render_inline_markdown(quote_text)) << "\n";
            } else {
                output_ << "  | " << quote_text << "\n";
            }
            continue;
        }

        // Regular paragraph line
        output_ << render_inline_markdown(line) << "\n";
    }

    // Handle unclosed code block
    if (in_code && !code_buffer.empty()) {
        render_code_block(code_buffer, code_lang);
    }
}

// ---------------------------------------------------------------------------
// Diff rendering
// ---------------------------------------------------------------------------

std::string TerminalRenderer::colorize_diff_line(const std::string& line) const {
    if (line.empty()) return line;

    if (starts_with(line, "+++") || starts_with(line, "---")) {
        return bold(line);
    }
    if (starts_with(line, "@@")) {
        return cyan(line);
    }
    if (line[0] == '+') {
        return green(line);
    }
    if (line[0] == '-') {
        return red(line);
    }
    return line;
}

void TerminalRenderer::render_diff(const std::string& unified_diff) {
    auto lines = split_lines(unified_diff);
    int line_num = 0;
    int gutter_width = 5;

    for (const auto& line : lines) {
        // Parse hunk headers for line numbers
        if (starts_with(line, "@@")) {
            line_num = parse_hunk_start(line);
            output_ << colorize_diff_line(line) << "\n";
            continue;
        }

        // File headers
        if (starts_with(line, "---") || starts_with(line, "+++") ||
            starts_with(line, "diff ") || starts_with(line, "index ")) {
            output_ << colorize_diff_line(line) << "\n";
            continue;
        }

        // Content lines with gutter
        std::string gutter;
        if (line.empty() || line[0] == ' ') {
            // Context line
            gutter = std::to_string(line_num);
            gutter = std::string(gutter_width - gutter.size(), ' ') + gutter;
            output_ << dim(gutter) << "  " << line << "\n";
            ++line_num;
        } else if (line[0] == '+') {
            gutter = std::to_string(line_num);
            gutter = std::string(gutter_width - gutter.size(), ' ') + gutter;
            output_ << dim(gutter) << " " << green(line) << "\n";
            ++line_num;
        } else if (line[0] == '-') {
            gutter = std::string(gutter_width, ' ');
            output_ << gutter << " " << red(line) << "\n";
        } else {
            output_ << colorize_diff_line(line) << "\n";
        }
    }
}

// ---------------------------------------------------------------------------
// Status bar
// ---------------------------------------------------------------------------

void TerminalRenderer::render_status_bar(const std::string& model, int input_tokens,
                                         int output_tokens, const std::string& session_id) {
    update_terminal_width();

    std::ostringstream bar;
    bar << " Model: " << model
        << " \xe2\x94\x82 Tokens: " << input_tokens << "/" << output_tokens;

    if (!session_id.empty()) {
        bar << " \xe2\x94\x82 Session: " << session_id.substr(0, 8);
    }
    bar << " ";

    std::string content = bar.str();
    std::string padded = pad_to(content, terminal_width_);

    if (overlay_status_bar_enabled_) {
        output_ << "\033[s"                                           // save cursor
                << "\033[" << terminal_height_ << ";1H"              // move to last row
                << kBgBlue << kFgWhite << padded << kReset           // render bar
                << "\033[u";                                         // restore cursor
    } else {
        output_ << dim(content) << "\n";
    }
    output_.flush();
}

void TerminalRenderer::hide_status_bar() {
    if (!overlay_status_bar_enabled_) return;

    output_ << "\033[s"
            << "\033[" << terminal_height_ << ";1H"
            << std::string(terminal_width_, ' ')
            << "\033[u";
    output_.flush();
}

// ---------------------------------------------------------------------------
// Banner
// ---------------------------------------------------------------------------

void TerminalRenderer::render_banner(const std::string& model, bool debug) {
    output_ << "\n";
    if (colors_enabled_) {
        output_ << " " << kBoldOn << kFgCyan << "\xe2\x9a\xa1 Bolt" << kReset
                << " \xe2\x80\x94 AI Coding Agent\n";
    } else {
        output_ << " Bolt -- AI Coding Agent\n";
    }
    output_ << dim("   Model: " + model) << "\n";
    output_ << dim(std::string("   Debug: ") + (debug ? "on" : "off")) << "\n";
    output_ << dim("   Tips:  /help for commands, @file to reference files, Ctrl+C to cancel") << "\n";
    output_ << "\n";
}

// ---------------------------------------------------------------------------
// Help
// ---------------------------------------------------------------------------

void TerminalRenderer::render_help(const std::vector<std::pair<std::string, std::string>>& commands_by_category) {
    int max_cmd_len = 0;
    for (const auto& [cmd, desc] : commands_by_category) {
        if (cmd.empty()) continue;  // category header
        int len = static_cast<int>(cmd.size());
        if (len > max_cmd_len) max_cmd_len = len;
    }

    for (const auto& [cmd, desc] : commands_by_category) {
        if (cmd.empty()) {
            // Category header
            output_ << "\n" << bold(desc) << "\n";
            continue;
        }
        std::string padded_cmd = cmd + std::string(max_cmd_len + 2 - cmd.size(), ' ');
        output_ << "  " << cyan(padded_cmd) << dim(desc) << "\n";
    }
    output_ << "\n";
}

// ---------------------------------------------------------------------------
// Box drawing
// ---------------------------------------------------------------------------

std::string TerminalRenderer::box_top(int width, const std::string& title,
                                      const std::string& color_code) const {
    if (title.empty()) {
        // ┌────────┐
        std::string line = "\xe2\x94\x8c";
        for (int i = 0; i < width - 2; ++i) line += "\xe2\x94\x80";
        line += "\xe2\x94\x90";
        return color(line, color_code);
    }

    // ┌─── Title ───┐
    int title_len = static_cast<int>(title.size());
    int remaining = width - 2 - title_len - 2;  // 2 borders, 2 spaces around title
    int left = 3;
    int right = remaining - left;
    if (right < 1) right = 1;

    std::string line = "\xe2\x94\x8c";
    for (int i = 0; i < left; ++i) line += "\xe2\x94\x80";
    line += " ";
    if (colors_enabled_) {
        line += std::string(kReset) + kBoldOn + title + kBoldOff + color_code;
    } else {
        line += title;
    }
    line += " ";
    for (int i = 0; i < right; ++i) line += "\xe2\x94\x80";
    line += "\xe2\x94\x90";
    return color(line, color_code);
}

std::string TerminalRenderer::box_line(int width, const std::string& content,
                                       const std::string& color_code) const {
    int inner = width - 4;  // 2 border chars + 2 padding spaces
    if (inner < 1) inner = 1;

    std::string padded = pad_to(content, inner);
    std::string border_l = color("\xe2\x94\x82", color_code);
    std::string border_r = color("\xe2\x94\x82", color_code);
    return border_l + " " + padded + " " + border_r;
}

std::string TerminalRenderer::box_bottom(int width, const std::string& color_code) const {
    std::string line = "\xe2\x94\x94";
    for (int i = 0; i < width - 2; ++i) line += "\xe2\x94\x80";
    line += "\xe2\x94\x98";
    return color(line, color_code);
}

std::string TerminalRenderer::box_separator(int width, const std::string& color_code) const {
    std::string line = "\xe2\x94\x9c";
    for (int i = 0; i < width - 2; ++i) line += "\xe2\x94\x80";
    line += "\xe2\x94\xa4";
    return color(line, color_code);
}

void TerminalRenderer::render_box(const std::string& title, const std::string& content,
                                  const std::string& color_code) {
    update_terminal_width();
    int box_width = std::min(terminal_width_, 80);

    output_ << box_top(box_width, title, color_code) << "\n";

    auto content_lines = split_lines(content);
    for (const auto& line : content_lines) {
        std::string wrapped = word_wrap(line, box_width - 4);
        auto wrapped_lines = split_lines(wrapped);
        for (const auto& wl : wrapped_lines) {
            output_ << box_line(box_width, wl, color_code) << "\n";
        }
    }

    output_ << box_bottom(box_width, color_code) << "\n";
}

// ---------------------------------------------------------------------------
// Approval card
// ---------------------------------------------------------------------------

void TerminalRenderer::render_approval_card(const std::string& tool_name, const std::string& risk,
                                            const std::string& reason, const std::string& summary,
                                            const std::string& details) {
    update_terminal_width();
    int box_width = std::min(terminal_width_, 80);

    // Choose border color based on risk
    std::string border_color = kFgYellow;
    if (risk == "low" || risk == "read_only") {
        border_color = kFgGreen;
    } else if (risk == "high" || risk == "destructive") {
        border_color = kFgRed;
    }

    output_ << "\n";
    output_ << box_top(box_width, tool_name, border_color) << "\n";

    // Risk line
    std::string risk_display = risk.empty() ? "unspecified" : risk;
    std::string risk_colored;
    if (risk == "low" || risk == "read_only") {
        risk_colored = green(risk_display);
    } else if (risk == "high" || risk == "destructive") {
        risk_colored = red(risk_display);
    } else {
        risk_colored = yellow(risk_display);
    }
    output_ << box_line(box_width, "Risk: " + risk_colored, border_color) << "\n";

    // Reason
    if (!reason.empty()) {
        output_ << box_line(box_width, "Reason: " + reason, border_color) << "\n";
    }

    // Summary
    if (!summary.empty()) {
        output_ << box_separator(box_width, border_color) << "\n";
        std::string wrapped = word_wrap(summary, box_width - 4);
        auto wrapped_lines = split_lines(wrapped);
        for (const auto& line : wrapped_lines) {
            output_ << box_line(box_width, line, border_color) << "\n";
        }
    }

    // Details (truncated)
    if (!details.empty()) {
        output_ << box_separator(box_width, border_color) << "\n";
        auto detail_lines = split_lines(details);
        int max_lines = 20;
        int count = 0;
        for (const auto& line : detail_lines) {
            if (count >= max_lines) {
                output_ << box_line(box_width, dim("... [truncated]"), border_color) << "\n";
                break;
            }
            output_ << box_line(box_width, line, border_color) << "\n";
            ++count;
        }
    }

    output_ << box_bottom(box_width, border_color) << "\n";

    // Action prompt
    output_ << dim("  [") << green("y") << dim("]es  [")
            << red("n") << dim("]o  [")
            << yellow("a") << dim("]lways  [")
            << cyan("d") << dim("]etails") << "\n";
    output_ << "  > ";
    output_.flush();
}

// ---------------------------------------------------------------------------
// Sessions list
// ---------------------------------------------------------------------------

void TerminalRenderer::render_sessions_list(
    const std::vector<std::tuple<std::string, int, std::string, std::string>>& sessions) {
    if (sessions.empty()) {
        output_ << dim("  No saved sessions.") << "\n";
        return;
    }

    // Header
    output_ << bold("  Sessions:") << "\n";
    output_ << dim("  " + std::string(terminal_width_ - 4, '-')) << "\n";

    for (const auto& [id, msg_count, modified, last_msg] : sessions) {
        std::string short_id = id.size() > 8 ? id.substr(0, 8) : id;
        output_ << "  " << cyan(short_id) << "  "
                << dim(std::to_string(msg_count) + " msgs") << "  "
                << dim(modified) << "  "
                << last_msg.substr(0, 40)
                << (last_msg.size() > 40 ? "..." : "")
                << "\n";
    }
    output_ << "\n";
}

// ---------------------------------------------------------------------------
// Cost summary
// ---------------------------------------------------------------------------

void TerminalRenderer::render_cost_summary(const std::string& model, int input_tokens,
                                           int output_tokens, double cost, int turns) {
    update_terminal_width();
    int box_width = std::min(terminal_width_, 60);

    std::ostringstream cost_str;
    cost_str << std::fixed << std::setprecision(4) << "$" << cost;

    output_ << "\n";
    output_ << box_top(box_width, "Session Summary", kFgCyan) << "\n";
    output_ << box_line(box_width, "Model:         " + model, kFgCyan) << "\n";
    output_ << box_line(box_width, "Turns:         " + std::to_string(turns), kFgCyan) << "\n";
    output_ << box_line(box_width, "Input tokens:  " + std::to_string(input_tokens), kFgCyan) << "\n";
    output_ << box_line(box_width, "Output tokens: " + std::to_string(output_tokens), kFgCyan) << "\n";
    output_ << box_separator(box_width, kFgCyan) << "\n";
    output_ << box_line(box_width, bold("Cost: " + cost_str.str()), kFgCyan) << "\n";
    output_ << box_bottom(box_width, kFgCyan) << "\n";
}

// ---------------------------------------------------------------------------
// Status info
// ---------------------------------------------------------------------------

void TerminalRenderer::render_status_info(const std::string& model, int input_tokens,
                                          int output_tokens, const std::string& session_id,
                                          const std::string& workspace, int file_count) {
    output_ << "\n";
    output_ << bold("Status") << "\n";
    output_ << dim("  Model:     ") << model << "\n";
    output_ << dim("  Tokens:    ") << input_tokens << " in / " << output_tokens << " out\n";
    if (!session_id.empty()) {
        output_ << dim("  Session:   ") << session_id.substr(0, 8) << "\n";
    }
    if (!workspace.empty()) {
        output_ << dim("  Workspace: ") << workspace << "\n";
    }
    if (file_count > 0) {
        output_ << dim("  Files:     ") << file_count << " indexed\n";
    }
    output_ << "\n";
}

// ---------------------------------------------------------------------------
// Streaming support
// ---------------------------------------------------------------------------

void TerminalRenderer::begin_stream() {
    stream_buffer_.clear();
    in_code_block_ = false;
    code_block_lang_.clear();
    stream_active_ = true;
}

void TerminalRenderer::stream_token(const std::string& token) {
    if (!stream_active_) return;

    // Output tokens immediately as they arrive for real-time streaming UX.
    // Users see text character by character as the model generates it.
    output_ << token << std::flush;
    stream_buffer_ += token;
}

void TerminalRenderer::end_stream() {
    if (!stream_active_) return;

    // stream_token() already outputs tokens in real-time,
    // so end_stream() just cleans up state without re-rendering.
    stream_buffer_.clear();
    in_code_block_ = false;
    code_block_lang_.clear();
    stream_active_ = false;
    output_.flush();
}
