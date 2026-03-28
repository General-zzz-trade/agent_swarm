#include "windows_ui_automation.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstdint>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#ifndef BM_CLICK
#define BM_CLICK 0x00F5
#endif

namespace {

std::wstring utf8_to_wstring(const std::string& value) {
    if (value.empty()) {
        return L"";
    }

    const int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                                           static_cast<int>(value.size()), nullptr, 0);
    if (length <= 0) {
        throw std::runtime_error("Failed to convert UTF-8 text to UTF-16");
    }

    std::wstring result(static_cast<std::size_t>(length), L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                            static_cast<int>(value.size()), result.data(), length) != length) {
        throw std::runtime_error("Failed to convert UTF-8 text to UTF-16");
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

    const int utf8_length = WideCharToMultiByte(CP_UTF8, 0, message.data(),
                                                static_cast<int>(message.size()), nullptr, 0,
                                                nullptr, nullptr);
    if (utf8_length <= 0) {
        return "Windows error " + std::to_string(error_code);
    }

    std::string result(static_cast<std::size_t>(utf8_length), '\0');
    if (WideCharToMultiByte(CP_UTF8, 0, message.data(), static_cast<int>(message.size()),
                            result.data(), utf8_length, nullptr, nullptr) != utf8_length) {
        return "Windows error " + std::to_string(error_code);
    }
    return result + " (code " + std::to_string(error_code) + ")";
}

std::string wstring_to_utf8(const std::wstring& value) {
    if (value.empty()) {
        return "";
    }

    const int utf8_length = WideCharToMultiByte(CP_UTF8, 0, value.data(),
                                                static_cast<int>(value.size()), nullptr, 0,
                                                nullptr, nullptr);
    if (utf8_length <= 0) {
        throw std::runtime_error("Failed to convert UTF-16 text to UTF-8");
    }

    std::string result(static_cast<std::size_t>(utf8_length), '\0');
    if (WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
                            result.data(), utf8_length, nullptr, nullptr) != utf8_length) {
        throw std::runtime_error("Failed to convert UTF-16 text to UTF-8");
    }
    return result;
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

UiElementInfo build_element_info(HWND hwnd) {
    UiElementInfo info;
    info.handle = window_handle_string(hwnd);
    const HWND parent = GetParent(hwnd);
    info.parent_handle = parent == nullptr ? "" : window_handle_string(parent);
    info.class_name = get_class_name_utf8(hwnd);
    info.text = get_window_text_utf8(hwnd);
    info.visible = IsWindowVisible(hwnd) != FALSE;
    info.enabled = IsWindowEnabled(hwnd) != FALSE;
    return info;
}

HWND resolve_root_window(const std::string& handle) {
    if (handle.empty()) {
        return GetForegroundWindow();
    }

    HWND hwnd = nullptr;
    if (!parse_handle_string(handle, &hwnd) || hwnd == nullptr || IsWindow(hwnd) == FALSE) {
        return nullptr;
    }
    return hwnd;
}

std::vector<UiElementInfo> enumerate_elements(HWND root, std::size_t max_elements) {
    std::vector<UiElementInfo> elements;
    if (root == nullptr) {
        return elements;
    }

    elements.push_back(build_element_info(root));
    struct EnumerationState {
        std::vector<UiElementInfo>* elements;
        std::size_t max_elements;
    } state{&elements, max_elements};

    EnumChildWindows(
        root,
        [](HWND child, LPARAM lparam) -> BOOL {
            auto* state = reinterpret_cast<EnumerationState*>(lparam);
            if (state->elements->size() >= state->max_elements) {
                return FALSE;
            }
            state->elements->push_back(build_element_info(child));
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&state));
    return elements;
}

bool matches_target(const UiElementInfo& element, const ClickElementTarget& target) {
    if (!target.element_handle.empty()) {
        return element.handle == target.element_handle;
    }

    if (!target.class_name.empty() &&
        to_lower_copy(element.class_name).find(to_lower_copy(target.class_name)) ==
            std::string::npos) {
        return false;
    }

    if (!target.text.empty() &&
        to_lower_copy(element.text).find(to_lower_copy(target.text)) == std::string::npos) {
        return false;
    }

    return !target.class_name.empty() || !target.text.empty();
}

ClickElementResult click_hwnd(HWND root, HWND target_hwnd) {
    if (root == nullptr || target_hwnd == nullptr) {
        return {false, {}, "Window handle was not found"};
    }

    if (IsIconic(root) != FALSE) {
        ShowWindow(root, SW_RESTORE);
    }
    (void)SetForegroundWindow(root);

    const UiElementInfo element = build_element_info(target_hwnd);
    const std::string lowered_class = to_lower_copy(element.class_name);
    if (lowered_class.find("button") != std::string::npos) {
        SendMessageW(target_hwnd, BM_CLICK, 0, 0);
        return {true, element, ""};
    }

    if (SetFocus(target_hwnd) == nullptr && GetLastError() != 0) {
        return {false, element, "SetFocus failed: " + format_windows_error(GetLastError())};
    }

    RECT rect{};
    if (GetClientRect(target_hwnd, &rect) != FALSE) {
        const int width = static_cast<int>(rect.right - rect.left);
        const int height = static_cast<int>(rect.bottom - rect.top);
        const int x = std::max(1, width / 2);
        const int y = std::max(1, height / 2);
        const LPARAM coords = MAKELPARAM(x, y);
        (void)SendMessageW(target_hwnd, WM_LBUTTONDOWN, MK_LBUTTON, coords);
        (void)SendMessageW(target_hwnd, WM_LBUTTONUP, 0, coords);
    }

    return {true, element, ""};
}

}  // namespace

InspectUiResult WindowsUiAutomation::inspect_ui(const InspectUiRequest& request) const {
    try {
        const HWND root = resolve_root_window(request.window_handle);
        if (root == nullptr || IsWindow(root) == FALSE) {
            return {false, "", "", {}, "No active window is available for UI inspection"};
        }

        InspectUiResult result;
        result.success = true;
        result.window_handle = window_handle_string(root);
        result.window_title = get_window_text_utf8(root);
        result.elements = enumerate_elements(root, std::max<std::size_t>(1, request.max_elements));
        return result;
    } catch (const std::exception& error) {
        return {false, "", "", {}, error.what()};
    }
}

ClickElementResult WindowsUiAutomation::click_element(const ClickElementTarget& target) const {
    try {
        const HWND root = resolve_root_window(target.window_handle);
        if (root == nullptr || IsWindow(root) == FALSE) {
            return {false, {}, "No active window is available for clicking"};
        }

        if (!target.element_handle.empty()) {
            HWND target_hwnd = nullptr;
            if (!parse_handle_string(target.element_handle, &target_hwnd) || target_hwnd == nullptr ||
                IsWindow(target_hwnd) == FALSE) {
                return {false, {}, "Element handle was not found"};
            }
            return click_hwnd(root, target_hwnd);
        }

        const std::vector<UiElementInfo> elements = enumerate_elements(root, 200);
        std::vector<UiElementInfo> matches;
        for (const UiElementInfo& element : elements) {
            if (matches_target(element, target)) {
                matches.push_back(element);
            }
        }

        if (matches.empty()) {
            return {false, {}, "No UI element matched the requested text/class filter"};
        }
        if (matches.size() > 1) {
            return {false, {}, "UI target matched multiple elements; refine the selector"};
        }

        HWND target_hwnd = nullptr;
        if (!parse_handle_string(matches.front().handle, &target_hwnd) || target_hwnd == nullptr ||
            IsWindow(target_hwnd) == FALSE) {
            return {false, {}, "Matched UI element handle is no longer valid"};
        }
        return click_hwnd(root, target_hwnd);
    } catch (const std::exception& error) {
        return {false, {}, error.what()};
    }
}

TypeTextResult WindowsUiAutomation::type_text(const std::string& text) const {
    try {
        const std::wstring wide_text = utf8_to_wstring(text);
        if (wide_text.empty()) {
            return {false, 0, "Text is empty"};
        }

        std::vector<INPUT> inputs;
        inputs.reserve(wide_text.size() * 2);
        for (const wchar_t ch : wide_text) {
            INPUT key_down{};
            key_down.type = INPUT_KEYBOARD;
            key_down.ki.wVk = 0;
            key_down.ki.wScan = ch;
            key_down.ki.dwFlags = KEYEVENTF_UNICODE;
            inputs.push_back(key_down);

            INPUT key_up = key_down;
            key_up.ki.dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP;
            inputs.push_back(key_up);
        }

        const UINT sent = SendInput(static_cast<UINT>(inputs.size()), inputs.data(), sizeof(INPUT));
        if (sent != inputs.size()) {
            return {false, 0, "SendInput failed: " + format_windows_error(GetLastError())};
        }

        return {true, wide_text.size(), ""};
    } catch (const std::exception& error) {
        return {false, 0, error.what()};
    }
}
