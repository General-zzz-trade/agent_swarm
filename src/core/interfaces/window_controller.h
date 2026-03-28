#ifndef CORE_INTERFACES_WINDOW_CONTROLLER_H
#define CORE_INTERFACES_WINDOW_CONTROLLER_H

#include <string>
#include <vector>

struct WindowInfo {
    std::string handle;
    unsigned long process_id = 0;
    std::string title;
    std::string class_name;
    bool visible = false;
};

struct WindowListResult {
    bool success = false;
    std::vector<WindowInfo> windows;
    std::string error;
};

struct WindowFocusTarget {
    std::string handle;
    std::string title;
};

struct WindowFocusResult {
    bool success = false;
    WindowInfo window;
    std::string error;
};

class IWindowController {
public:
    virtual ~IWindowController() = default;

    virtual WindowListResult list_windows() const = 0;
    virtual WindowFocusResult focus_window(const WindowFocusTarget& target) const = 0;
};

#endif
