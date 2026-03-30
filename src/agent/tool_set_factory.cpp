#include "tool_set_factory.h"

#include <cstdlib>
#include <memory>
#include <stdexcept>

#include "build_and_test_tool.h"
#include "plugin_loader.h"
#include "swarm_coordinator.h"
#include "calculator_tool.h"
#include "click_element_tool.h"
#include "code_intel_tool.h"
#include "delete_file_tool.h"
#include "edit_file_tool.h"
#include "git_tool.h"
#include "focus_window_tool.h"
#include "inspect_ui_tool.h"
#include "list_dir_tool.h"
#include "list_processes_tool.h"
#include "list_windows_tool.h"
#include "open_app_tool.h"
#include "read_file_tool.h"
#include "run_command_tool.h"
#include "search_code_tool.h"
#include "task_planner_tool.h"
#include "type_text_tool.h"
#include "wait_for_window_tool.h"
#include "web_fetch_tool.h"
#include "web_search_tool.h"
#include "write_file_tool.h"

ToolRegistry create_default_tool_registry(
    const std::filesystem::path& workspace_root,
    const std::shared_ptr<IFileSystem>& file_system,
    const std::shared_ptr<ICommandRunner>& command_runner,
    const std::shared_ptr<IAuditLogger>& audit_logger,
    CommandPolicyConfig command_policy,
    const std::shared_ptr<IProcessManager>& process_manager,
    const std::shared_ptr<IUiAutomation>& ui_automation,
    const std::shared_ptr<IWindowController>& window_controller,
    const std::shared_ptr<IHttpTransport>& http_transport) {
    if (file_system == nullptr) {
        throw std::invalid_argument("Tool set factory requires a file system");
    }
    if (command_runner == nullptr) {
        throw std::invalid_argument("Tool set factory requires a command runner");
    }

    ToolRegistry tools;
    tools.register_tool(std::make_unique<CalculatorTool>());
    tools.register_tool(std::make_unique<EditFileTool>(workspace_root, file_system, audit_logger));
    tools.register_tool(std::make_unique<ListDirTool>(workspace_root, file_system));
    tools.register_tool(std::make_unique<ReadFileTool>(workspace_root, file_system));
    tools.register_tool(
        std::make_unique<WriteFileTool>(workspace_root, file_system, audit_logger));
    tools.register_tool(
        std::make_unique<DeleteFileTool>(workspace_root, file_system, audit_logger));
    tools.register_tool(std::make_unique<SearchCodeTool>(workspace_root, file_system));
    tools.register_tool(std::make_unique<GitTool>(workspace_root, command_runner));
    tools.register_tool(std::make_unique<CodeIntelTool>(workspace_root, file_system));
    tools.register_tool(
        std::make_unique<RunCommandTool>(workspace_root, command_runner, audit_logger,
                                         std::move(command_policy)));
    tools.register_tool(
        std::make_unique<BuildAndTestTool>(workspace_root, command_runner, audit_logger));
    tools.register_tool(std::make_unique<TaskPlannerTool>());
    if (process_manager != nullptr) {
        tools.register_tool(std::make_unique<ListProcessesTool>(process_manager));
        tools.register_tool(std::make_unique<OpenAppTool>(process_manager, audit_logger));
    }
    if (window_controller != nullptr) {
        tools.register_tool(std::make_unique<ListWindowsTool>(window_controller));
        tools.register_tool(std::make_unique<FocusWindowTool>(window_controller, audit_logger));
        tools.register_tool(std::make_unique<WaitForWindowTool>(window_controller));
    }
    if (ui_automation != nullptr) {
        tools.register_tool(std::make_unique<InspectUiTool>(ui_automation));
        tools.register_tool(std::make_unique<ClickElementTool>(ui_automation, audit_logger));
        tools.register_tool(std::make_unique<TypeTextTool>(ui_automation, audit_logger));
    }
    if (http_transport != nullptr) {
        tools.register_tool(std::make_unique<WebFetchTool>(http_transport));
        tools.register_tool(std::make_unique<WebSearchTool>(http_transport));
    }
    // Load plugins from workspace and global dirs
    PluginLoader plugin_loader(command_runner);

    // Workspace plugins: .bolt/plugins/
    auto ws_plugins_dir = workspace_root / ".bolt" / "plugins";
    for (auto& tool : plugin_loader.load_plugins(ws_plugins_dir)) {
        tools.register_tool(std::move(tool));
    }

    // Global plugins: ~/.bolt/plugins/
    const char* home = std::getenv("HOME");
    if (home) {
        auto global_plugins_dir = std::filesystem::path(home) / ".bolt" / "plugins";
        for (auto& tool : plugin_loader.load_plugins(global_plugins_dir)) {
            tools.register_tool(std::move(tool));
        }
    }

    return tools;
}
