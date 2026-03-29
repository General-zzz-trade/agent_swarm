# Contributing to Bolt

Thank you for your interest in contributing! This guide will help you get started.

## Development Setup

### Prerequisites

- C++17 compiler (GCC 9+, Clang 10+, or MSVC 2019+)
- CMake 3.10+
- Ollama (optional, for local model testing)

### Build

```bash
git clone https://github.com/General-zzz-trade/bolt.git
cd bolt
cmake -B build -S .
cmake --build build -j8
```

### Run tests

```bash
./build/kernel_tests          # 76 unit tests
./build/agent_integration_tests  # 3 integration tests
./build/capability_tests      # 41 capability tests
```

All 120 tests must pass before submitting a PR.

### Run the agent

```bash
# With Ollama
ollama pull qwen3:8b
./build/bolt agent "Hello"

# Web UI
./build/bolt web-chat --port 8080
```

## How to Contribute

### Adding a new tool

1. Create `src/agent/your_tool.h` and `src/agent/your_tool.cpp`
2. Inherit from `Tool` base class in `src/agent/tool.h`
3. Implement `name()`, `description()`, `run()`, and optionally `schema()`, `is_read_only()`
4. Register in `src/agent/tool_set_factory.cpp`
5. Add to policy config in `src/core/config/policy_config.h`
6. Add to `CMakeLists.txt` (all targets that link agent code)
7. Add tests in `tests/capability_tests.cpp`

### Adding a new LLM provider

1. Create `src/providers/your_provider.h` and `.cpp`
2. Implement `IModelClient` interface from `src/core/interfaces/model_client.h`
3. Must implement: `generate()`, `model()`, `chat()`, `chat_streaming()`
4. Set `supports_tools()` and `supports_streaming()` flags
5. Add to factory in `src/app/model_client_factory.cpp`
6. Add config keys in `src/app/app_config.cpp`

### Cross-platform porting

The main Windows-specific code is in `src/platform/windows/`. To add Linux/macOS:

1. Create `src/platform/linux/` or `src/platform/macos/`
2. Implement the interfaces in `src/core/interfaces/`:
   - `IHttpTransport` (use libcurl instead of WinHTTP)
   - `ICommandRunner` (use fork/exec instead of CreateProcess)
   - `IFileSystem` (already mostly cross-platform via std::filesystem)
   - `IProcessManager` (use /proc on Linux)
   - `IWindowController` (X11/Wayland on Linux, AppKit on macOS)
   - `IUiAutomation` (AT-SPI on Linux, Accessibility API on macOS)
3. Add platform detection in `CMakeLists.txt`

## Code Style

- C++17 standard
- 4-space indentation
- `snake_case` for functions and variables
- `PascalCase` for classes and types
- `UPPER_CASE` for constants
- Prefix interfaces with `I` (e.g., `IModelClient`)
- Use `std::unique_ptr` for exclusive ownership, `std::shared_ptr` for shared
- Prefer `const` references for function parameters
- All new code must have tests

## Pull Request Process

1. Fork the repository
2. Create a feature branch: `git checkout -b feature/your-feature`
3. Make your changes
4. Run all tests: `./build/kernel_tests && ./build/agent_integration_tests && ./build/capability_tests`
5. Commit with a clear message
6. Push and create a Pull Request

## Areas We Need Help With

- **Linux/macOS support** (high priority)
- **Tree-sitter integration** for proper AST-based code intelligence
- **MCP protocol** for standardized tool communication
- **Docker image** for easy deployment
- **VS Code extension** for IDE integration
- **More test coverage** for edge cases
- **Documentation** and examples
