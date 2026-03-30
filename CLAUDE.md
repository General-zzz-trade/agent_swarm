# Bolt

Blazing-fast autonomous coding agent written in C++17. Single binary, zero dependencies.

## Build

```bash
cmake -B build -S .
cmake --build build -j$(nproc)
```

Debug with sanitizers:
```bash
cmake -B build -S . -DENABLE_SANITIZERS=ON
cmake --build build -j$(nproc)
```

Release:
```bash
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc) --target bolt
```

## Test

```bash
./build/kernel_tests              # 70 unit tests
./build/agent_integration_tests   # 3 integration tests
./build/capability_tests          # 41 capability tests
./build/e2e_tests                 # 9 E2E tests
./build/sse_parser_tests          # 10 SSE parser tests
./build/mcp_server_tests          # 8 MCP server tests
./build/approval_provider_tests   # 6 approval provider tests
./build/api_e2e_tests             # 12 API E2E tests
```

All tests must pass before PR. Run all:
```bash
./build/kernel_tests && ./build/agent_integration_tests && ./build/capability_tests && ./build/e2e_tests && ./build/sse_parser_tests && ./build/mcp_server_tests && ./build/approval_provider_tests && ./build/api_e2e_tests
```

## Run

```bash
./build/bolt                              # Interactive mode
./build/bolt agent "prompt here"          # Single-turn
./build/bolt agent -p                     # Pipe mode
./build/bolt agent --resume               # Resume last session
./build/bolt web-chat --port 8080         # Web UI
./build/bolt api-server --port 9090       # REST API
./build/bolt mcp-server                   # MCP server
./build/bolt telegram                     # Telegram bot
./build/bolt discord                      # Discord bot
./build/bolt wechat                       # WeChat bot
./build/bolt slack                        # Slack bot
./build/bolt bench --rounds 5             # Benchmark
```

## Interactive Commands

```
Session:    /save [name]  /load <id>  /sessions  /delete <id>  /export [file]  /memory
Context:    /clear  /compact  /undo  /reset  /context
Display:    /model  /cost  /status  /debug  /diff  /doctor
Mode:       /plan  /auto
System:     /quit  /help  /init  /sandbox  /plugins  /skills  /team  /bench

Shortcuts:  Ctrl+C cancel  Ctrl+L clear  Ctrl+D exit  ↑/↓ history  Tab complete
File ref:   @file or @file:10-20 to include file contents in prompt
```

## Architecture

```
src/
  agent/        # Tools (27 built-in + plugins) and agent loop
    speculative_executor  # Predictive tool execution during LLM streaming
    plugin_loader         # External plugin discovery and loading
  app/          # CLI, config, factories, runners, terminal UI
    terminal_renderer   # Markdown rendering, diff display, status bar
    terminal_input      # Readline-like input with tab completion
    signal_handler      # Ctrl+C cancellation, SIGWINCH resize
    token_tracker       # Token usage and cost estimation
    rate_limiter        # HTTP request rate limiting
    wechat_bot          # WeChat gateway
    slack_bot           # Slack gateway
  core/
    caching/    # Tool result cache
    config/     # Runtime/policy configs
    indexing/   # Trigram file index, semantic index, prefetch
    interfaces/ # Abstract interfaces (I-prefixed)
    mcp/        # MCP server protocol
    model/      # Chat message, tool schema, token usage types
    net/        # SSE parser
    routing/    # Model router, prompt compressor
    session/    # Session persistence, persistent memory
    threading/  # Thread pool
  platform/
    linux/      # Linux implementations (libcurl, fork/exec, /proc)
    windows/    # Windows implementations (WinHTTP, CreateProcess)
  providers/    # LLM clients: OpenAI, Claude, Gemini, Ollama, Groq, DeepSeek,
                #   Qwen, GLM, Moonshot, Baichuan, Doubao
tests/          # 8 test executables, 159 total tests
web/            # Web UI (dark/light theme, Markdown, SSE streaming)
vscode-extension/  # VS Code sidebar chat extension
npm/            # npm package (bolt-agent)
```

## Adding a New Tool

1. Create `src/agent/your_tool.h` + `.cpp`, inherit from `Tool` (`src/agent/tool.h`)
2. Implement: `name()`, `description()`, `run()`, optionally `schema()`, `is_read_only()`
3. Register in `src/agent/tool_set_factory.cpp`
4. Add to policy config in `src/core/config/policy_config.h`
5. Add source to `bolt_core` in `CMakeLists.txt`
6. Add tests in `tests/capability_tests.cpp`

## Adding a New LLM Provider

1. Create `src/providers/your_provider.h` + `.cpp`, implement `IModelClient` (`src/core/interfaces/model_client.h`)
2. Must implement: `generate()`, `model()`, `chat()`, `chat_streaming()`
3. Set `supports_tools()` and `supports_streaming()` flags
4. Parse `TokenUsage` from API response into `ChatMessage::usage`
5. Add to factory in `src/app/model_client_factory.cpp`
6. Add config keys in `src/app/app_config.cpp`

## Code Style

- C++17 standard, 4-space indentation
- `snake_case` for functions/variables, `PascalCase` for classes/types, `UPPER_CASE` for constants
- Prefix interfaces with `I` (e.g., `IModelClient`, `IHttpTransport`)
- `std::unique_ptr` for exclusive ownership, `std::shared_ptr` for shared
- Prefer `const` references for parameters
- All new code must have tests

## Gotchas

- CMakeLists.txt has separate `target_sources` blocks for WIN32 vs UNIX — new platform files go in the right block
- Provider sources (openai, claude, gemini, ollama) are added per-platform, not globally
- `bolt_core` is a static library linked by both the main executable and all test targets
- Ollama is the default local provider (no API key needed): `ollama pull qwen3:8b`
- The `third_party/` dir is included globally via `include_directories()` — headers available everywhere
- `TerminalInput` uses raw termios mode — falls back to `std::getline` when not a TTY (pipes, tests)
- `TerminalApprovalProvider` has two constructors: legacy (istream/ostream) for tests, rich (renderer/input) for interactive
- Token usage is parsed per-provider: Claude `usage`, OpenAI `usage`, Gemini `usageMetadata`, Ollama `eval_count`, DeepSeek/Qwen/GLM/Moonshot/Baichuan/Doubao via OpenAI-compatible `usage`
- WeChat bot requires `WECHAT_WEBHOOK_URL` (default `http://localhost:3001`); Slack bot requires `SLACK_BOT_TOKEN` and `SLACK_CHANNEL_ID`
- `send_email` tool requires `SENDGRID_API_KEY` env var; optional `BOLT_EMAIL_FROM` for sender address
- `browser` tool requires headless Chrome/Chromium in PATH
- `SandboxedCommandRunner` wraps `LinuxCommandRunner` with bubblewrap (`bwrap`) for OS-level isolation; falls back to unsandboxed if bwrap is not installed
- Sandbox config keys live under `sandbox.*` in bolt.conf; env overrides are `BOLT_SANDBOX_ENABLED` and `BOLT_SANDBOX_NETWORK`
- Plugins live in `.bolt/plugins/` (workspace) or `~/.bolt/plugins/` (global), each with a `plugin.json` manifest
- Plugin tools execute as subprocesses: JSON args via stdin, result via stdout; they inherit sandbox restrictions
- `SpeculativeExecutor` runs read-only tools during streaming; only safe (read-only) tools are speculated, never write tools
- `MemoryStore` persists cross-session facts to `~/.bolt/memory.json` (global) and `.bolt/memory.json` (workspace)
- Auto-verify runs `build_and_test` after code edits; max 3 retries; disable with `agent.auto_verify=false`
