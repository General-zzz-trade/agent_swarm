#include "static_approval_provider.h"

StaticApprovalProvider::StaticApprovalProvider(bool decision)
    : decision_(decision) {}

bool StaticApprovalProvider::approve(const ApprovalRequest& request) {
    (void)request;
    return decision_;
}
