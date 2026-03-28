#ifndef APP_APPROVAL_PROVIDER_FACTORY_H
#define APP_APPROVAL_PROVIDER_FACTORY_H

#include <iosfwd>
#include <memory>

#include "../core/config/approval_config.h"
#include "../core/interfaces/approval_provider.h"

std::shared_ptr<IApprovalProvider> create_approval_provider(const ApprovalConfig& config,
                                                            std::istream& input,
                                                            std::ostream& output);

#endif
