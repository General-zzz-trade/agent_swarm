#pragma once
#include <atomic>
#include <csignal>
#ifndef _WIN32
#include <sys/ioctl.h>
#include <unistd.h>
#endif

class SignalHandler {
public:
    static SignalHandler& instance();

    void install();

    bool is_cancelled() const;
    void set_cancelled(bool value);
    void reset();

    bool resize_pending() const;
    void clear_resize_pending();
    int terminal_width() const;
    int terminal_height() const;
    void update_dimensions();

private:
    SignalHandler() = default;
    std::atomic<bool> cancelled_{false};
    std::atomic<bool> resize_pending_{false};
    std::atomic<int> terminal_width_{80};
    std::atomic<int> terminal_height_{24};

    static void sigint_handler(int sig);
    static void sigwinch_handler(int sig);
};
