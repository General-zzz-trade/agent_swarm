#ifndef APP_TERMINAL_APPROVAL_PROVIDER_H
#define APP_TERMINAL_APPROVAL_PROVIDER_H

#include <iosfwd>

#include "../core/interfaces/approval_provider.h"

class TerminalApprovalProvider : public IApprovalProvider {
public:
    TerminalApprovalProvider(std::istream& input, std::ostream& output);

    bool approve(const ApprovalRequest& request) override;

private:
    std::istream& input_;
    std::ostream& output_;
};

#endif
