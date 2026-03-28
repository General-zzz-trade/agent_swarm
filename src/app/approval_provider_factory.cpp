#include "approval_provider_factory.h"

#include <stdexcept>

#include "static_approval_provider.h"
#include "terminal_approval_provider.h"

std::shared_ptr<IApprovalProvider> create_approval_provider(const ApprovalConfig& config,
                                                            std::istream& input,
                                                            std::ostream& output) {
    switch (config.mode) {
        case ApprovalMode::prompt:
            return std::make_shared<TerminalApprovalProvider>(input, output);
        case ApprovalMode::auto_approve:
            return std::make_shared<StaticApprovalProvider>(true);
        case ApprovalMode::auto_deny:
            return std::make_shared<StaticApprovalProvider>(false);
    }

    throw std::runtime_error("Unsupported approval mode");
}
