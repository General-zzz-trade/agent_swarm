#ifndef PLATFORM_WINDOWS_WINDOWS_WINDOW_CONTROLLER_H
#define PLATFORM_WINDOWS_WINDOWS_WINDOW_CONTROLLER_H

#include "../../core/interfaces/window_controller.h"

class WindowsWindowController : public IWindowController {
public:
    WindowListResult list_windows() const override;
    WindowFocusResult focus_window(const WindowFocusTarget& target) const override;
};

#endif
