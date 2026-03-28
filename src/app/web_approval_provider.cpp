#include "web_approval_provider.h"

bool WebApprovalProvider::approve(const ApprovalRequest& request) {
    std::unique_lock<std::mutex> lock(mutex_);
    pending_request_ = request;
    has_pending_request_ = true;
    resolved_ = false;
    approved_ = false;

    condition_.wait(lock, [this] { return resolved_; });

    const bool approved = approved_;
    has_pending_request_ = false;
    resolved_ = false;
    approved_ = false;
    pending_request_ = {};
    return approved;
}

WebApprovalSnapshot WebApprovalProvider::snapshot() const {
    std::lock_guard<std::mutex> lock(mutex_);
    WebApprovalSnapshot snapshot;
    snapshot.has_pending_request = has_pending_request_;
    if (has_pending_request_) {
        snapshot.request = pending_request_;
    }
    return snapshot;
}

bool WebApprovalProvider::resolve(bool approved) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!has_pending_request_ || resolved_) {
        return false;
    }
    approved_ = approved;
    resolved_ = true;
    condition_.notify_all();
    return true;
}
