#ifndef APP_FILE_AUDIT_LOGGER_H
#define APP_FILE_AUDIT_LOGGER_H

#include <filesystem>
#include <mutex>

#include "../core/interfaces/audit_logger.h"

class FileAuditLogger : public IAuditLogger {
public:
    explicit FileAuditLogger(std::filesystem::path log_path);

    void log(const AuditEvent& event) override;

private:
    std::filesystem::path log_path_;
    std::mutex mutex_;
};

#endif
