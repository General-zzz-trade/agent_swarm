cmake_minimum_required(VERSION 3.10)
project(bolt)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(ENABLE_SANITIZERS "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
if(ENABLE_SANITIZERS)
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
endif()

include_directories(${CMAKE_SOURCE_DIR}/third_party)

enable_testing()

# --- Shared static library ---
add_library(bolt_core STATIC
        src/core/threading/thread_pool.cpp
        src/core/indexing/file_index.cpp
        src/core/indexing/file_prefetch.cpp
        src/core/indexing/semantic_index.cpp
        src/core/routing/model_router.cpp
        src/core/routing/prompt_compressor.cpp
        src/core/caching/tool_result_cache.cpp
        src/core/session/session_store.cpp
        src/core/session/memory_store.cpp
        src/core/mcp/mcp_server.cpp
        src/core/net/sse_parser.cpp
        src/agent/agent.cpp
        src/agent/action_parser.cpp
        src/agent/build_and_test_tool.cpp
        src/agent/calculator_tool.cpp
        src/agent/click_element_tool.cpp
        src/agent/code_intel_tool.cpp
        src/agent/delete_file_tool.cpp
        src/agent/edit_file_tool.cpp
        src/agent/git_tool.cpp
        src/agent/focus_window_tool.cpp
        src/agent/inspect_ui_tool.cpp
        src/agent/list_dir_tool.cpp
        src/agent/list_processes_tool.cpp
        src/agent/list_windows_tool.cpp
        src/agent/open_app_tool.cpp
        src/agent/permission_policy.cpp
        src/agent/plugin_tool.cpp
        src/agent/plugin_loader.cpp
        src/agent/read_file_tool.cpp
        src/agent/run_command_tool.cpp
        src/agent/search_code_tool.cpp
        src/agent/swarm_coordinator.cpp
        src/agent/task_planner_tool.cpp
        src/agent/task_runner.cpp
        src/agent/type_text_tool.cpp
        src/agent/tool_set_factory.cpp
        src/agent/tool_registry.cpp
        src/agent/wait_for_window_tool.cpp
        src/agent/web_fetch_tool.cpp
        src/agent/web_search_tool.cpp
        src/agent/workspace_prompt.cpp
        src/agent/workspace_utils.cpp
        src/agent/write_file_tool.cpp
        src/app/app_config.cpp
        src/app/agent_factory.cpp
        src/app/agent_cli_options.cpp
        src/app/agent_runner.cpp
        src/app/terminal_input.cpp
        src/app/approval_provider_factory.cpp
        src/app/file_audit_logger.cpp
        src/app/program_cli.cpp
        src/app/self_check_runner.cpp
        src/app/static_approval_provider.cpp
        src/app/terminal_approval_provider.cpp
        src/app/terminal_renderer.cpp
        src/app/signal_handler.cpp
        src/app/token_tracker.cpp
        src/app/web_approval_provider.cpp
        src/app/setup_wizard.cpp
        src/app/web_chat_cli_options.cpp
        src/platform/platform_agent_factory.cpp
)

if (WIN32)
    target_sources(bolt_core PRIVATE
        src/platform/windows/windows_agent_factory.cpp
        src/platform/windows/ollama_json_utils.cpp
        src/platform/windows/winhttp_transport.cpp
        src/platform/windows/windows_command_runner.cpp
        src/platform/windows/windows_file_system.cpp
        src/platform/windows/windows_process_manager.cpp
        src/platform/windows/windows_ui_automation.cpp
        src/platform/windows/windows_window_controller.cpp
        src/platform/windows/windows_ollama_model_client.cpp
        src/providers/openai_client.cpp
        src/providers/claude_client.cpp
        src/providers/gemini_client.cpp
        src/providers/ollama_chat_client.cpp
        src/app/model_client_factory.cpp
    )
    target_link_libraries(bolt_core PUBLIC user32 winhttp ws2_32)
elseif (UNIX)
    target_sources(bolt_core PRIVATE
        src/platform/linux/linux_agent_factory.cpp
        src/platform/linux/linux_file_system.cpp
        src/platform/linux/linux_command_runner.cpp
        src/platform/linux/sandboxed_command_runner.cpp
        src/platform/linux/linux_process_manager.cpp
        src/platform/linux/linux_http_transport.cpp
        src/providers/openai_client.cpp
        src/providers/claude_client.cpp
        src/providers/gemini_client.cpp
        src/providers/ollama_chat_client.cpp
        src/app/model_client_factory.cpp
    )
    find_package(CURL REQUIRED)
    target_link_libraries(bolt_core PUBLIC pthread CURL::libcurl)
endif()

# --- Main executable ---
add_executable(bolt
        src/main.cpp
        src/matrix.cpp
        src/demo/training_demo.cpp
        src/app/benchmark_runner.cpp
        src/app/web_chat_server.cpp
)
if (WIN32)
    target_sources(bolt PRIVATE src/platform/windows/http2_transport.cpp)
endif()
target_link_libraries(bolt PRIVATE bolt_core)

# --- Tests ---
add_executable(kernel_tests tests/kernel_tests.cpp)
target_link_libraries(kernel_tests PRIVATE bolt_core)
add_test(NAME kernel_tests COMMAND kernel_tests)

add_executable(agent_integration_tests tests/agent_integration_tests.cpp)
target_link_libraries(agent_integration_tests PRIVATE bolt_core)
add_test(NAME agent_integration_tests COMMAND agent_integration_tests)

add_executable(capability_tests tests/capability_tests.cpp)
target_link_libraries(capability_tests PRIVATE bolt_core)
add_test(NAME capability_tests COMMAND capability_tests)

add_executable(e2e_tests tests/e2e_tests.cpp)
target_link_libraries(e2e_tests PRIVATE bolt_core)
add_test(NAME e2e_tests COMMAND e2e_tests)

add_executable(sse_parser_tests tests/sse_parser_tests.cpp)
target_link_libraries(sse_parser_tests PRIVATE bolt_core)
add_test(NAME sse_parser_tests COMMAND sse_parser_tests)

add_executable(mcp_server_tests tests/mcp_server_tests.cpp)
target_link_libraries(mcp_server_tests PRIVATE bolt_core)
add_test(NAME mcp_server_tests COMMAND mcp_server_tests)

add_executable(approval_provider_tests tests/approval_provider_tests.cpp)
target_link_libraries(approval_provider_tests PRIVATE bolt_core)
add_test(NAME approval_provider_tests COMMAND approval_provider_tests)
