#ifndef AGENT_PERMISSION_POLICY_H
#define AGENT_PERMISSION_POLICY_H

#include <string>

#include "../core/config/policy_config.h"
#include "action.h"

struct PolicyDecision {
    bool allowed;
    bool approval_required;
    std::string effective_risk;
    std::string reason;
};

class PermissionPolicy {
public:
    explicit PermissionPolicy(PolicyConfig config = {});

    PolicyDecision evaluate(const Action& action) const;

private:
    PolicyConfig config_;
};

#endif
