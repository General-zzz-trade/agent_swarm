#include "signal_handler.h"

#ifdef _WIN32
#include <windows.h>
#endif

SignalHandler& SignalHandler::instance() {
    static SignalHandler handler;
    return handler;
}

void SignalHandler::install() {
    update_dimensions();

#ifdef _WIN32
    ::SetConsoleCtrlHandler([](DWORD type) -> BOOL {
        if (type == CTRL_C_EVENT) {
            instance().cancelled_.store(true, std::memory_order_release);
            return TRUE;
        }
        return FALSE;
    }, TRUE);
#else
    struct sigaction sa_int{};
    sa_int.sa_handler = sigint_handler;
    sigemptyset(&sa_int.sa_mask);
    sa_int.sa_flags = 0;
    sigaction(SIGINT, &sa_int, nullptr);

    struct sigaction sa_winch{};
    sa_winch.sa_handler = sigwinch_handler;
    sigemptyset(&sa_winch.sa_mask);
    sa_winch.sa_flags = 0;
    sigaction(SIGWINCH, &sa_winch, nullptr);
#endif
}

bool SignalHandler::is_cancelled() const {
    return cancelled_.load(std::memory_order_acquire);
}

void SignalHandler::set_cancelled(bool value) {
    cancelled_.store(value, std::memory_order_release);
}

void SignalHandler::reset() {
    cancelled_.store(false, std::memory_order_release);
}

bool SignalHandler::resize_pending() const {
    return resize_pending_.load(std::memory_order_acquire);
}

void SignalHandler::clear_resize_pending() {
    resize_pending_.store(false, std::memory_order_release);
}

int SignalHandler::terminal_width() const {
    return terminal_width_.load(std::memory_order_relaxed);
}

int SignalHandler::terminal_height() const {
    return terminal_height_.load(std::memory_order_relaxed);
}

void SignalHandler::update_dimensions() {
#ifdef _WIN32
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi)) {
        int w = csbi.srWindow.Right - csbi.srWindow.Left + 1;
        int h = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
        if (w > 0) terminal_width_.store(w, std::memory_order_relaxed);
        if (h > 0) terminal_height_.store(h, std::memory_order_relaxed);
    }
#else
    struct winsize ws{};
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0) {
        if (ws.ws_col > 0) {
            terminal_width_.store(ws.ws_col, std::memory_order_relaxed);
        }
        if (ws.ws_row > 0) {
            terminal_height_.store(ws.ws_row, std::memory_order_relaxed);
        }
    }
#endif
}

#ifndef _WIN32
void SignalHandler::sigint_handler(int /*sig*/) {
    instance().cancelled_.store(true, std::memory_order_release);
}

void SignalHandler::sigwinch_handler(int /*sig*/) {
    instance().resize_pending_.store(true, std::memory_order_release);
}
#endif
