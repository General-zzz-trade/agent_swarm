cmake_minimum_required(VERSION 3.10)
project(mini_nn_cpp)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

enable_testing()

add_executable(mini_nn
        src/app/app_config.cpp
        src/app/agent_factory.cpp
        src/app/agent_cli_options.cpp
        src/app/agent_runner.cpp
        src/app/approval_provider_factory.cpp
        src/app/file_audit_logger.cpp
        src/app/program_cli.cpp
        src/app/self_check_runner.cpp
        src/app/static_approval_provider.cpp
        src/app/terminal_approval_provider.cpp
        src/app/web_approval_provider.cpp
        src/app/web_chat_cli_options.cpp
        src/app/web_chat_server.cpp
        src/main.cpp
        src/matrix.cpp
        src/platform/platform_agent_factory.cpp
        src/demo/training_demo.cpp
        src/agent/agent.cpp
        src/agent/action_parser.cpp
        src/agent/calculator_tool.cpp
        src/agent/click_element_tool.cpp
        src/agent/edit_file_tool.cpp
        src/agent/focus_window_tool.cpp
        src/agent/inspect_ui_tool.cpp
        src/agent/list_dir_tool.cpp
        src/agent/list_processes_tool.cpp
        src/agent/list_windows_tool.cpp
        src/agent/open_app_tool.cpp
        src/agent/permission_policy.cpp
        src/agent/read_file_tool.cpp
        src/agent/run_command_tool.cpp
        src/agent/search_code_tool.cpp
        src/agent/task_runner.cpp
        src/agent/type_text_tool.cpp
        src/agent/tool_set_factory.cpp
        src/agent/tool_registry.cpp
        src/agent/wait_for_window_tool.cpp
        src/agent/workspace_utils.cpp
        src/agent/write_file_tool.cpp
)

if (WIN32)
    target_sources(mini_nn PRIVATE
        src/platform/windows/windows_agent_factory.cpp
        src/platform/windows/ollama_json_utils.cpp
        src/platform/windows/windows_command_runner.cpp
        src/platform/windows/windows_file_system.cpp
        src/platform/windows/windows_process_manager.cpp
        src/platform/windows/windows_ui_automation.cpp
        src/platform/windows/windows_window_controller.cpp
        src/platform/windows/windows_ollama_model_client.cpp
    )
    target_link_libraries(mini_nn PRIVATE user32 winhttp ws2_32)
elseif (UNIX AND NOT APPLE)
    target_sources(mini_nn PRIVATE
            src/platform/linux/linux_agent_factory.cpp
    )
endif()

add_executable(kernel_tests
        tests/kernel_tests.cpp
        src/agent/action_parser.cpp
        src/agent/agent.cpp
        src/agent/calculator_tool.cpp
        src/agent/click_element_tool.cpp
        src/agent/edit_file_tool.cpp
        src/agent/focus_window_tool.cpp
        src/agent/inspect_ui_tool.cpp
        src/agent/list_dir_tool.cpp
        src/agent/list_processes_tool.cpp
        src/agent/list_windows_tool.cpp
        src/agent/open_app_tool.cpp
        src/agent/permission_policy.cpp
        src/agent/read_file_tool.cpp
        src/agent/run_command_tool.cpp
        src/agent/search_code_tool.cpp
        src/agent/task_runner.cpp
        src/agent/type_text_tool.cpp
        src/agent/tool_set_factory.cpp
        src/agent/tool_registry.cpp
        src/agent/wait_for_window_tool.cpp
        src/agent/workspace_utils.cpp
        src/agent/write_file_tool.cpp
        src/app/app_config.cpp
        src/app/agent_factory.cpp
        src/app/agent_cli_options.cpp
        src/app/agent_runner.cpp
        src/app/approval_provider_factory.cpp
        src/app/file_audit_logger.cpp
        src/app/program_cli.cpp
        src/app/self_check_runner.cpp
        src/app/static_approval_provider.cpp
        src/app/terminal_approval_provider.cpp
        src/app/web_chat_cli_options.cpp
        src/platform/platform_agent_factory.cpp
)

if (WIN32)
    target_sources(kernel_tests PRIVATE
            src/platform/windows/windows_agent_factory.cpp
            src/platform/windows/ollama_json_utils.cpp
            src/platform/windows/windows_command_runner.cpp
            src/platform/windows/windows_file_system.cpp
            src/platform/windows/windows_process_manager.cpp
            src/platform/windows/windows_ui_automation.cpp
            src/platform/windows/windows_window_controller.cpp
            src/platform/windows/windows_ollama_model_client.cpp
    )
    target_link_libraries(kernel_tests PRIVATE user32 winhttp)
elseif (UNIX AND NOT APPLE)
    target_sources(kernel_tests PRIVATE
            src/platform/linux/linux_agent_factory.cpp
    )
endif()

add_test(NAME kernel_tests COMMAND kernel_tests)

add_executable(agent_integration_tests
        tests/agent_integration_tests.cpp
        src/agent/action_parser.cpp
        src/agent/agent.cpp
        src/agent/calculator_tool.cpp
        src/agent/click_element_tool.cpp
        src/agent/edit_file_tool.cpp
        src/agent/focus_window_tool.cpp
        src/agent/inspect_ui_tool.cpp
        src/agent/list_dir_tool.cpp
        src/agent/list_processes_tool.cpp
        src/agent/list_windows_tool.cpp
        src/agent/open_app_tool.cpp
        src/agent/permission_policy.cpp
        src/agent/read_file_tool.cpp
        src/agent/run_command_tool.cpp
        src/agent/search_code_tool.cpp
        src/agent/task_runner.cpp
        src/agent/type_text_tool.cpp
        src/agent/tool_set_factory.cpp
        src/agent/tool_registry.cpp
        src/agent/wait_for_window_tool.cpp
        src/agent/workspace_utils.cpp
        src/agent/write_file_tool.cpp
        src/app/app_config.cpp
        src/app/agent_factory.cpp
        src/app/agent_cli_options.cpp
        src/app/agent_runner.cpp
        src/app/approval_provider_factory.cpp
        src/app/file_audit_logger.cpp
        src/app/program_cli.cpp
        src/app/static_approval_provider.cpp
        src/app/terminal_approval_provider.cpp
        src/app/web_chat_cli_options.cpp
        src/platform/windows/ollama_json_utils.cpp
)

add_test(NAME agent_integration_tests COMMAND agent_integration_tests)
