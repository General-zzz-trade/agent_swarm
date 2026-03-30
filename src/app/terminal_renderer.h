#pragma once
#include <iostream>
#include <string>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/ioctl.h>
#include <unistd.h>
#endif

struct ApprovalRequest;  // forward declare

class TerminalRenderer {
public:
    explicit TerminalRenderer(std::ostream& output);

    // Terminal dimensions
    int terminal_width() const;
    void update_terminal_width();

    // Core rendering
    void render_markdown(const std::string& text);
    void render_diff(const std::string& unified_diff);
    void render_status_bar(const std::string& model, int input_tokens,
                           int output_tokens, const std::string& session_id);
    void hide_status_bar();
    void render_approval_card(const std::string& tool_name, const std::string& risk,
                              const std::string& reason, const std::string& summary,
                              const std::string& details);
    void render_banner(const std::string& model, bool debug);
    void render_help(const std::vector<std::pair<std::string, std::string>>& commands_by_category);
    void render_box(const std::string& title, const std::string& content,
                    const std::string& color_code);
    void render_sessions_list(const std::vector<std::tuple<std::string, int, std::string, std::string>>& sessions);
    void render_cost_summary(const std::string& model, int input_tokens, int output_tokens,
                             double cost, int turns);
    void render_status_info(const std::string& model, int input_tokens, int output_tokens,
                            const std::string& session_id, const std::string& workspace,
                            int file_count);

    // Streaming support
    void begin_stream();
    void stream_token(const std::string& token);
    void end_stream();

    // Utilities
    std::string word_wrap(const std::string& text, int width) const;

    // Color check
    bool colors_enabled() const { return colors_enabled_; }

private:
    std::ostream& output_;
    int terminal_width_ = 80;
    int terminal_height_ = 24;
    bool colors_enabled_ = true;

    // Streaming state
    bool in_code_block_ = false;
    std::string code_block_lang_;
    std::string stream_buffer_;
    bool stream_active_ = false;

    // ANSI helpers
    std::string bold(const std::string& s) const;
    std::string dim(const std::string& s) const;
    std::string italic(const std::string& s) const;
    std::string color(const std::string& s, const std::string& code) const;
    std::string green(const std::string& s) const;
    std::string red(const std::string& s) const;
    std::string yellow(const std::string& s) const;
    std::string cyan(const std::string& s) const;
    std::string magenta(const std::string& s) const;
    std::string bg_color(const std::string& s, const std::string& code) const;

    // Markdown internals
    std::string render_inline_markdown(const std::string& line) const;
    void render_code_block(const std::string& code, const std::string& lang);
    std::string highlight_code_keywords(const std::string& code) const;

    // Diff internals
    std::string colorize_diff_line(const std::string& line) const;

    // Box drawing
    std::string box_top(int width, const std::string& title, const std::string& color_code) const;
    std::string box_line(int width, const std::string& content, const std::string& color_code) const;
    std::string box_bottom(int width, const std::string& color_code) const;
    std::string box_separator(int width, const std::string& color_code) const;
};
