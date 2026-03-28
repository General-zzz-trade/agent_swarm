#ifndef AGENT_READ_FILE_TOOL_H
#define AGENT_READ_FILE_TOOL_H

#include <filesystem>
#include <memory>

#include "../core/interfaces/file_system.h"
#include "tool.h"

class ReadFileTool : public Tool {
public:
    ReadFileTool(std::filesystem::path workspace_root,
                 std::shared_ptr<IFileSystem> file_system);

    std::string name() const override;
    std::string description() const override;
    ToolResult run(const std::string& args) const override;

private:
    std::filesystem::path workspace_root_;
    std::shared_ptr<IFileSystem> file_system_;
};

#endif
