#ifndef PLATFORM_WINDOWS_WINDOWS_UI_AUTOMATION_H
#define PLATFORM_WINDOWS_WINDOWS_UI_AUTOMATION_H

#include "../../core/interfaces/ui_automation.h"

class WindowsUiAutomation : public IUiAutomation {
public:
    InspectUiResult inspect_ui(const InspectUiRequest& request) const override;
    ClickElementResult click_element(const ClickElementTarget& target) const override;
    TypeTextResult type_text(const std::string& text) const override;
};

#endif
