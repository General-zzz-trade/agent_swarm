#include "windows_window_controller.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstdint>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include <windows.h>

namespace {

std::string wstring_to_utf8(const std::wstring& value) {
    if (value.empty()) {
        return "";
    }

    const int length = WideCharToMultiByte(CP_UTF8, 0, value.data(),
                                           static_cast<int>(value.size()), nullptr, 0, nullptr,
                                           nullptr);
    if (length <= 0) {
        throw std::runtime_error("Failed to convert UTF-16 text to UTF-8");
    }

    std::string result(static_cast<std::size_t>(length), '\0');
    if (WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
                            result.data(), length, nullptr, nullptr) != length) {
        throw std::runtime_error("Failed to convert UTF-16 text to UTF-8");
    }
    return result;
}

std::string format_windows_error(DWORD error_code) {
    LPWSTR message_buffer = nullptr;
    const DWORD length = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
            FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, error_code, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<LPWSTR>(&message_buffer), 0, nullptr);

    std::wstring message =
        length > 0 && message_buffer != nullptr ? std::wstring(message_buffer, length) : L"";
    if (message_buffer != nullptr) {
        LocalFree(message_buffer);
    }

    while (!message.empty() &&
           (message.back() == L'\r' || message.back() == L'\n' || message.back() == L' ')) {
        message.pop_back();
    }

    if (message.empty()) {
        return "Windows error " + std::to_string(error_code);
    }
    return wstring_to_utf8(message) + " (code " + std::to_string(error_code) + ")";
}

std::string to_lower_copy(const std::string& value) {
    std::string lowered = value;
    std::transform(lowered.begin(), lowered.end(), lowered.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return lowered;
}

std::string window_handle_string(HWND hwnd) {
    std::ostringstream output;
    output << "0x" << std::hex << std::uppercase
           << static_cast<std::uintptr_t>(reinterpret_cast<std::uintptr_t>(hwnd));
    return output.str();
}

std::string get_window_text_utf8(HWND hwnd) {
    const int length = GetWindowTextLengthW(hwnd);
    if (length <= 0) {
        return "";
    }

    std::wstring text(static_cast<std::size_t>(length + 1), L'\0');
    const int copied = GetWindowTextW(hwnd, text.data(), length + 1);
    if (copied <= 0) {
        return "";
    }
    text.resize(static_cast<std::size_t>(copied));
    return wstring_to_utf8(text);
}

std::string get_class_name_utf8(HWND hwnd) {
    std::wstring class_name(256, L'\0');
    const int copied = GetClassNameW(hwnd, class_name.data(),
                                     static_cast<int>(class_name.size()));
    if (copied <= 0) {
        return "";
    }
    class_name.resize(static_cast<std::size_t>(copied));
    return wstring_to_utf8(class_name);
}

WindowInfo build_window_info(HWND hwnd) {
    WindowInfo info;
    info.handle = window_handle_string(hwnd);
    DWORD process_id = 0;
    GetWindowThreadProcessId(hwnd, &process_id);
    info.process_id = static_cast<unsigned long>(process_id);
    info.title = get_window_text_utf8(hwnd);
    info.class_name = get_class_name_utf8(hwnd);
    info.visible = IsWindowVisible(hwnd) != FALSE;
    return info;
}

std::vector<WindowInfo> enumerate_windows() {
    std::vector<WindowInfo> windows;
    EnumWindows(
        [](HWND hwnd, LPARAM lparam) -> BOOL {
            auto* output = reinterpret_cast<std::vector<WindowInfo>*>(lparam);
            if (IsWindowVisible(hwnd) == FALSE) {
                return TRUE;
            }

            WindowInfo info = build_window_info(hwnd);
            if (info.title.empty()) {
                return TRUE;
            }
            output->push_back(std::move(info));
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&windows));
    return windows;
}

bool parse_handle_string(const std::string& value, HWND* hwnd) {
    std::string trimmed = value;
    trimmed.erase(trimmed.begin(),
                  std::find_if(trimmed.begin(), trimmed.end(),
                               [](unsigned char ch) { return !std::isspace(ch); }));
    trimmed.erase(std::find_if(trimmed.rbegin(), trimmed.rend(),
                               [](unsigned char ch) { return !std::isspace(ch); })
                      .base(),
                  trimmed.end());
    if (trimmed.empty()) {
        return false;
    }

    int base = 10;
    if (trimmed.rfind("0x", 0) == 0 || trimmed.rfind("0X", 0) == 0) {
        base = 16;
        trimmed = trimmed.substr(2);
    }

    char* end = nullptr;
    const unsigned long long parsed = std::strtoull(trimmed.c_str(), &end, base);
    if (end == nullptr || *end != '\0') {
        return false;
    }

    *hwnd = reinterpret_cast<HWND>(static_cast<std::uintptr_t>(parsed));
    return true;
}

}  // namespace

WindowListResult WindowsWindowController::list_windows() const {
    try {
        std::vector<WindowInfo> windows = enumerate_windows();
        std::sort(windows.begin(), windows.end(),
                  [](const WindowInfo& left, const WindowInfo& right) {
                      if (left.title != right.title) {
                          return left.title < right.title;
                      }
                      return left.handle < right.handle;
                  });
        return {true, std::move(windows), ""};
    } catch (const std::exception& error) {
        return {false, {}, error.what()};
    }
}

WindowFocusResult WindowsWindowController::focus_window(const WindowFocusTarget& target) const {
    try {
        HWND hwnd = nullptr;
        if (!target.handle.empty()) {
            if (!parse_handle_string(target.handle, &hwnd) || hwnd == nullptr ||
                IsWindow(hwnd) == FALSE) {
                return {false, {}, "Window handle was not found"};
            }
        } else if (!target.title.empty()) {
            const std::string expected = to_lower_copy(target.title);
            std::vector<WindowInfo> matches;
            for (const WindowInfo& info : enumerate_windows()) {
                if (to_lower_copy(info.title) == expected) {
                    matches.push_back(info);
                }
            }
            if (matches.empty()) {
                return {false, {}, "Window title was not found"};
            }
            if (matches.size() > 1) {
                return {false, {}, "Window title matched multiple visible windows"};
            }
            parse_handle_string(matches.front().handle, &hwnd);
        } else {
            return {false, {}, "Window focus target is empty"};
        }

        if (hwnd == nullptr || IsWindow(hwnd) == FALSE) {
            return {false, {}, "Window handle was not found"};
        }

        if (IsIconic(hwnd) != FALSE) {
            ShowWindow(hwnd, SW_RESTORE);
        }
        if (SetForegroundWindow(hwnd) == FALSE) {
            return {false, {}, "SetForegroundWindow failed: " +
                                   format_windows_error(GetLastError())};
        }

        WindowInfo info = build_window_info(hwnd);
        return {true, std::move(info), ""};
    } catch (const std::exception& error) {
        return {false, {}, error.what()};
    }
}
