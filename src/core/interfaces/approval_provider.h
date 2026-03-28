#ifndef CORE_INTERFACES_APPROVAL_PROVIDER_H
#define CORE_INTERFACES_APPROVAL_PROVIDER_H

#include <string>

struct ApprovalRequest {
    std::string tool_name;
    std::string args;
    std::string reason;
    std::string risk;
    std::string preview_summary;
    std::string preview_details;
};

class IApprovalProvider {
public:
    virtual ~IApprovalProvider() = default;
    virtual bool approve(const ApprovalRequest& request) = 0;
};

#endif
