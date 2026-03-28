#ifndef APP_STATIC_APPROVAL_PROVIDER_H
#define APP_STATIC_APPROVAL_PROVIDER_H

#include "../core/interfaces/approval_provider.h"

class StaticApprovalProvider : public IApprovalProvider {
public:
    explicit StaticApprovalProvider(bool decision);

    bool approve(const ApprovalRequest& request) override;

private:
    bool decision_;
};

#endif
