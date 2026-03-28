#ifndef APP_NULL_AUDIT_LOGGER_H
#define APP_NULL_AUDIT_LOGGER_H

#include "../core/interfaces/audit_logger.h"

class NullAuditLogger : public IAuditLogger {
public:
    void log(const AuditEvent&) override {}
};

#endif
