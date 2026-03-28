#include "permission_policy.h"

namespace {

bool contains(const std::unordered_set<std::string>& values, const std::string& value) {
    return values.find(value) != values.end();
}

std::string bounded_side_effect_risk(const std::string& risk) {
    if (risk == "high") {
        return "high";
    }
    return "medium";
}

}  // namespace

PermissionPolicy::PermissionPolicy(PolicyConfig config)
    : config_(std::move(config)) {}

PolicyDecision PermissionPolicy::evaluate(const Action& action) const {
    if (action.type == ActionType::reply) {
        return {true, false, "none", "Replies are always allowed"};
    }

    if (config_.block_high_risk && action.risk == "high") {
        return {false, false, "high", "High-risk actions are blocked by policy"};
    }

    if (contains(config_.read_only_tools, action.tool_name)) {
        return {true, false, action.risk.empty() ? "low" : action.risk,
                "Read-only tool is allowed"};
    }

    if (contains(config_.bounded_write_tools, action.tool_name)) {
        return {false, true, bounded_side_effect_risk(action.risk),
                "Workspace-limited write requires user approval"};
    }

    if (contains(config_.bounded_command_tools, action.tool_name)) {
        return {false, true, bounded_side_effect_risk(action.risk),
                "Whitelist-limited command execution requires user approval"};
    }

    if (contains(config_.bounded_desktop_tools, action.tool_name)) {
        return {false, true, bounded_side_effect_risk(action.risk),
                "Desktop control requires user approval"};
    }

    if (action.requires_confirmation) {
        return {false, true, bounded_side_effect_risk(action.risk),
                "Action requires user approval"};
    }

    return {false, false, "blocked", "Tool is not permitted by policy"};
}
