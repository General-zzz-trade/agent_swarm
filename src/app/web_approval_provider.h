#ifndef APP_WEB_APPROVAL_PROVIDER_H
#define APP_WEB_APPROVAL_PROVIDER_H

#include <condition_variable>
#include <mutex>

#include "../core/interfaces/approval_provider.h"

struct WebApprovalSnapshot {
    bool has_pending_request = false;
    ApprovalRequest request;
};

class WebApprovalProvider : public IApprovalProvider {
public:
    bool approve(const ApprovalRequest& request) override;

    WebApprovalSnapshot snapshot() const;
    bool resolve(bool approved);

private:
    mutable std::mutex mutex_;
    std::condition_variable condition_;
    bool has_pending_request_ = false;
    bool resolved_ = false;
    bool approved_ = false;
    ApprovalRequest pending_request_;
};

#endif
