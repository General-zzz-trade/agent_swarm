#ifndef AGENT_WRITE_FILE_TOOL_H
#define AGENT_WRITE_FILE_TOOL_H

#include <filesystem>
#include <memory>

#include "../core/interfaces/audit_logger.h"
#include "../core/interfaces/file_system.h"
#include "tool.h"

class WriteFileTool : public Tool {
public:
    WriteFileTool(std::filesystem::path workspace_root,
                  std::shared_ptr<IFileSystem> file_system,
                  std::shared_ptr<IAuditLogger> audit_logger = nullptr);

    std::string name() const override;
    std::string description() const override;
    ToolPreview preview(const std::string& args) const override;
    ToolResult run(const std::string& args) const override;

private:
    std::filesystem::path workspace_root_;
    std::shared_ptr<IFileSystem> file_system_;
    std::shared_ptr<IAuditLogger> audit_logger_;
};

#endif
