#ifndef CORE_INTERFACES_UI_AUTOMATION_H
#define CORE_INTERFACES_UI_AUTOMATION_H

#include <cstddef>
#include <string>
#include <vector>

struct UiElementInfo {
    std::string handle;
    std::string parent_handle;
    std::string class_name;
    std::string text;
    bool visible = false;
    bool enabled = false;
};

struct InspectUiRequest {
    std::string window_handle;
    std::size_t max_elements = 40;
};

struct InspectUiResult {
    bool success = false;
    std::string window_handle;
    std::string window_title;
    std::vector<UiElementInfo> elements;
    std::string error;
};

struct ClickElementTarget {
    std::string window_handle;
    std::string element_handle;
    std::string text;
    std::string class_name;
};

struct ClickElementResult {
    bool success = false;
    UiElementInfo element;
    std::string error;
};

struct TypeTextResult {
    bool success = false;
    std::size_t characters_sent = 0;
    std::string error;
};

class IUiAutomation {
public:
    virtual ~IUiAutomation() = default;

    virtual InspectUiResult inspect_ui(const InspectUiRequest& request) const = 0;
    virtual ClickElementResult click_element(const ClickElementTarget& target) const = 0;
    virtual TypeTextResult type_text(const std::string& text) const = 0;
};

#endif
