#ifndef CORE_CONFIG_APPROVAL_CONFIG_H
#define CORE_CONFIG_APPROVAL_CONFIG_H

enum class ApprovalMode {
    prompt,
    auto_approve,
    auto_deny,
};

struct ApprovalConfig {
    ApprovalMode mode = ApprovalMode::prompt;
};

#endif
