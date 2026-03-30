# Bolt

<!-- Badges placeholder -->
<!--
[![Build](https://github.com/General-zzz-trade/Bolt/actions/workflows/ci.yml/badge.svg)](https://github.com/General-zzz-trade/Bolt/actions)
[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
[![npm](https://img.shields.io/npm/v/bolt-agent)](https://www.npmjs.com/package/bolt-agent)
-->

A blazing-fast autonomous coding agent written in C++17. Single binary, zero dependencies, 0.2ms framework overhead.

> A local, native alternative to Claude Code / Cursor / Aider that ships as one executable with no runtime dependencies.

[English](#features) | [中文](README_CN.md)

---

## Why C++?

| | Python agents | Bolt |
|---|---|---|
| **Startup** | 2-5 seconds | **<50ms** |
| **Framework overhead** | 10-50ms/turn | **0.2ms/turn** |
| **Deployment** | Python + pip + venv | **Single binary** |
| **Memory** | 200-500MB | **<30MB** |
| **Parallel tools** | GIL-limited | **True multi-core** |
| **Streaming** | Buffered | **Zero-copy SSE** |
| **Sandbox** | None built-in | **Bubblewrap / Seatbelt** |
| **Speculative exec** | N/A | **Read-only tools run during streaming** |

---

## Features

- **23 built-in tools** -- file ops, code search, code intelligence, build & test, git, shell, web fetch, web search, headless browser, task planning, desktop automation
- **Plugin system** -- extend with tools written in any language (Python, Node, Go, Rust, etc.)
- **SKILL.md system** -- reusable prompt templates with YAML frontmatter and auto-load
- **6 LLM providers** -- Ollama (local), OpenAI, Claude, Gemini, Groq, model router + any OpenAI-compatible API
- **8 running modes** -- interactive terminal, single-turn, web UI, REST API, MCP server, Telegram bot, Discord bot, benchmark
- **Autonomous loop** -- edit -> compile -> test -> fix -> repeat (auto-verify with up to 3 retries)
- **Agent Team** -- parallel multi-agent execution with git worktree isolation
- **Speculative execution** -- read-only tools start running before the LLM finishes streaming
- **Sandbox** -- Bubblewrap (Linux) and Seatbelt (macOS) for OS-level command isolation
- **Persistent memory** -- cross-session facts stored in `~/.bolt/memory.json`
- **Session persistence** -- save/restore conversations, resume last session with `--resume`
- **Code intelligence** -- find definitions, references, classes across C++/Python/JS/Rust/Go
- **Setup wizard** -- first-run interactive provider and model selection
- **VS Code extension** -- sidebar chat, explain/fix selection, generate tests
- **Web UI** -- dark/light theme, Markdown rendering, code highlighting, SSE streaming
- **Performance engine** -- thread pool, trigram index, speculative prefetch, HTTP/2, connection pool
- **Audit logging** -- all write operations logged to file for security review
- **Cross-platform** -- Linux, macOS, Windows
- **Docker** -- multi-stage build, sandbox included

---

## Quick Start

### Install via npm

```bash
npm install -g bolt-agent
bolt agent "Hello, what tools do you have?"
```

### Build from source

```bash
git clone https://github.com/General-zzz-trade/Bolt.git
cd Bolt
cmake -B build -S .
cmake --build build -j$(nproc)
./build/bolt agent "Read src/main.cpp and explain it"
```

**Requirements**: C++17 compiler (GCC 9+, Clang 10+, MSVC 2019+), CMake 3.10+, libcurl (Linux/macOS)

### With Ollama (local AI, no API key needed)

```bash
# Install Ollama: https://ollama.ai
ollama pull qwen3:8b
bolt agent "Read CMakeLists.txt and list all build targets"
```

### Docker

```bash
docker build -t bolt .
docker run -v $(pwd):/workspace bolt agent "List the files"
```

The Docker image uses a multi-stage build. The runtime image includes libcurl, bubblewrap (sandbox), git, and CA certificates.

---

## Running Modes

```bash
# Interactive terminal (default) -- readline, Markdown rendering, @file references
bolt

# Single-turn agent
bolt agent "Search the codebase for TODO comments"

# Resume last session
bolt agent --resume

# Web UI (dark/light theme, SSE streaming)
bolt web-chat --port 8080

# REST API server
bolt api-server --port 9090

# MCP server (for Claude Code / Cursor integration)
bolt mcp-server

# Telegram bot
TELEGRAM_BOT_TOKEN=... bolt telegram

# Discord bot
DISCORD_BOT_TOKEN=... DISCORD_CHANNEL_ID=... bolt discord

# Performance benchmark
bolt bench --rounds 5
bolt bench --json > results.json
```

---

## First-Time Setup Wizard

On first launch, Bolt runs an interactive setup wizard that guides you through:

1. **Provider selection** -- Ollama (local), OpenAI, Claude, Gemini, Groq
2. **Model selection** -- pick from available models for your chosen provider
3. **API key entry** -- stored in `~/.bolt/config.json`

Re-run the model selector at any time with the `/model` interactive command.

---

## Tools

### File & Code

| Tool | Description | Read-only |
|------|-------------|:---------:|
| `read_file` | Read file contents (up to 32KB) | Yes |
| `list_dir` | List directory contents | Yes |
| `write_file` | Create new files | No |
| `edit_file` | Modify existing files (exact text replacement) | No |
| `delete_file` | Delete files | No |
| `search_code` | Full-text search across workspace | Yes |
| `code_intel` | Find definitions, references, classes, includes | Yes |

### Build & Run

| Tool | Description | Read-only |
|------|-------------|:---------:|
| `build_and_test` | Auto-detect build system, compile, run tests | No |
| `run_command` | Shell commands (35+ whitelisted tools, full git) | No |
| `git` | Git operations (status, diff, log, commit, etc.) | Yes |
| `calculator` | Arithmetic expressions | Yes |
| `task_planner` | Create and track multi-step plans | Yes |

### Web & Browser

| Tool | Description | Read-only |
|------|-------------|:---------:|
| `web_fetch` | Fetch URL and extract text content | Yes |
| `web_search` | Search the web via DuckDuckGo | Yes |
| `browser` | Headless Chrome: navigate, screenshot, extract text | Yes |

### Desktop Automation

| Tool | Description | Read-only |
|------|-------------|:---------:|
| `list_processes` | List running processes | Yes |
| `open_app` | Launch applications | No |
| `list_windows` | List visible windows | Yes |
| `focus_window` | Bring window to foreground | No |
| `wait_for_window` | Wait for window to appear | Yes |
| `inspect_ui` | Inspect UI element tree | Yes |
| `click_element` | Click UI elements | No |
| `type_text` | Type text into focused window | No |

---

## Plugin System

Extend Bolt with tools written in any language. Plugins communicate via JSON over stdin/stdout.

**Directory structure:**
```
.bolt/plugins/my-plugin/     # Workspace-local plugin
~/.bolt/plugins/my-plugin/   # Global plugin
  plugin.json                # Manifest (required)
  my_tool.py                 # Executable
```

**plugin.json manifest:**
```json
{
  "name": "my-plugin",
  "version": "1.0.0",
  "tools": [
    {
      "name": "my_tool",
      "description": "Does something useful",
      "command": "python3 my_tool.py"
    }
  ]
}
```

**Example plugin (Python):**
```python
#!/usr/bin/env python3
import json, sys
req = json.loads(input())
if req["method"] == "describe":
    print(json.dumps({"name": "my_tool", "description": "My custom tool"}))
elif req["method"] == "run":
    result = f"Got args: {req['params']['args']}"
    print(json.dumps({"success": True, "result": result}))
```

Plugin tools inherit sandbox restrictions and appear alongside built-in tools.

---

## SKILL.md System

Skills are reusable prompt templates stored as Markdown files with YAML frontmatter.

```markdown
---
name: Code Review
description: Guidelines for reviewing code
auto_load: true
---

When reviewing code, check for:
1. Error handling
2. Edge cases
3. Performance implications
...
```

Place `SKILL.md` files in `.bolt/skills/` (workspace) or `~/.bolt/skills/` (global). Skills with `auto_load: true` are injected into every prompt automatically.

---

## Sandbox

Bolt sandboxes all shell commands for OS-level isolation.

| Platform | Technology | Mechanism |
|----------|-----------|-----------|
| Linux | [Bubblewrap](https://github.com/containers/bubblewrap) (`bwrap`) | Mount namespaces, seccomp |
| macOS | Seatbelt (`sandbox-exec`) | macOS sandbox profiles |

**Configuration** (`bolt.conf`):
```ini
sandbox.enabled = true
sandbox.network = true
sandbox.allow_write = /tmp, /var/tmp
sandbox.deny_read = ~/.ssh, ~/.aws, ~/.gnupg
```

**Environment overrides:**
```bash
export BOLT_SANDBOX_ENABLED=true
export BOLT_SANDBOX_NETWORK=false
```

Falls back to unsandboxed execution if bwrap/seatbelt is not installed.

---

## Performance

```
=== Framework Overhead (no network) ===
  Agent loop:               0.2ms
  JSON serialize (10 tools): 0.2ms
  Tool lookup (1000x):      0.1ms  (O(1) hash)
  8 tools parallel (pool):  1.2ms  vs sequential 4.0ms  (3.4x speedup)
  File index build:         173ms  (178 files, 1.5MB)
  Prefetch cache:           593x faster than disk
  Speculative execution:    Read-only tools start during LLM streaming

=== Ollama qwen3:8b (local) ===
  Cold connect:             236ms
  TTFT:                     164ms
  Token throughput:         14.9 tok/s
```

**Key optimizations:**
- **Speculative executor** -- detects tool call patterns in partial streaming output and pre-executes read-only tools
- **HTTP/2 + connection pool** -- persistent connections to LLM providers
- **Trigram file index** -- sub-millisecond code search across the workspace
- **Thread pool** -- parallel tool execution with true multi-core utilization
- **Prompt compressor** -- reduces context size via model router (fast/strong provider split)

---

## Security

| Layer | Mechanism |
|-------|-----------|
| **OS sandbox** | Bubblewrap (Linux) / Seatbelt (macOS) isolate all shell commands |
| **Permission policy** | Tools classified as read-only or write; write tools require approval |
| **Command whitelist** | 35+ pre-approved commands; others need explicit approval |
| **Audit log** | All write operations logged to `.bolt/audit.log` |
| **Sensitive path blocking** | `~/.ssh`, `~/.aws`, `~/.gnupg`, `/etc/shadow` blocked by default |
| **Network control** | Optional domain whitelist for outbound requests |
| **Approval modes** | `auto-approve`, `ask` (interactive), or custom policy |

---

## Interactive Features

### Slash Commands

| Category | Commands |
|----------|----------|
| **Session** | `/save [name]`  `/load <id>`  `/sessions`  `/delete <id>`  `/export [file]`  `/memory` |
| **Context** | `/clear`  `/compact`  `/undo`  `/reset` |
| **Display** | `/model`  `/cost`  `/status`  `/debug`  `/diff` |
| **System** | `/quit`  `/help`  `/sandbox`  `/plugins` |

### Keyboard Shortcuts

| Shortcut | Action |
|----------|--------|
| `Ctrl+C` | Cancel current operation |
| `Ctrl+L` | Clear screen |
| `Ctrl+D` | Exit |
| `Up/Down` | Command history |
| `Tab` | Autocomplete |

### @file References

Include file contents directly in your prompt:

```
> Explain @src/main.cpp
> What does @src/agent/tool.h:10-30 do?
```

---

## Configuration

### bolt.conf

Create `bolt.conf` in your project root:

```ini
# Provider: ollama | ollama-chat | openai | claude | gemini | groq | router
provider = ollama-chat

# Models
openai.model = gpt-4o
claude.model = claude-sonnet-4-20250514
gemini.model = gemini-2.0-flash

# Router mode: use fast model for simple tasks, strong for complex
router.fast_provider = groq
router.strong_provider = claude

# Approval
approval.mode = auto-approve

# Sandbox
sandbox.enabled = true
sandbox.network = true

# Agent behavior
agent.auto_verify = true
agent.max_retries = 3
```

### Environment Variables

```bash
export OPENAI_API_KEY=sk-...
export ANTHROPIC_API_KEY=sk-ant-...
export GEMINI_API_KEY=AI...
export GROQ_API_KEY=gsk_...
export TELEGRAM_BOT_TOKEN=...
export DISCORD_BOT_TOKEN=...
export DISCORD_CHANNEL_ID=...
export BOLT_SANDBOX_ENABLED=true
export BOLT_PROVIDER=claude
```

---

## Architecture

```
                         +-----------------+
                         |   LLM Provider  |
                         | (Ollama/OpenAI/ |
                         |Claude/Gemini/   |
                         | Groq/Router)    |
                         +--------+--------+
                                  |
                    +-------------v--------------+
                    |        Agent Loop          |     0.2ms overhead
                    | (50 steps max, auto-verify)|
                    +-------------+--------------+
                                  |
          +-----------+-----------+-----------+-----------+
          |           |           |           |           |
   +------v---+ +----v-----+ +--v------+ +--v------+ +--v--------+
   |Tool Engine| |Speculative| |Thread  | |File     | |  Swarm    |
   |(23 tools) | |Executor   | |Pool    | |Index    | |Coordinator|
   |+ plugins  | |(streaming)| |(N-core)| |(trigram) | |(team mode)|
   +------+---+ +----------+ +--------+ +---------+ +-----------+
          |
   +------v---------------------------------------------------+
   |                   Workspace (your code)                    |
   +----+------------+------------+------------+---------------+
        |            |            |            |
   +----v----+ +----v----+ +----v----+ +-----v------+
   |MCP Server| |Plugin   | |Session  | |Memory Store|
   |(JSON-RPC)| |System   | |Store    | |(cross-sess)|
   +---------+ +---------+ +---------+ +------------+
        |            |
   +----v----+ +----v--------+
   |Sandbox  | |Audit Logger |
   |(bwrap/  | |(file-based) |
   |seatbelt)| +-------------+
   +---------+

   Frontends:
   +----------+ +--------+ +--------+ +---------+ +---------+
   |Terminal  | |Web UI  | |REST API| |Telegram | |Discord  |
   |(readline)| |(SSE)   | |Server  | |Bot      | |Bot      |
   +----------+ +--------+ +--------+ +---------+ +---------+
```

---

## Project Structure

```
src/
  agent/            # Agent loop, 23 tools, plugins, skills, speculative executor, swarm
  app/              # CLI, web server, API server, Telegram/Discord bots, setup wizard
  core/
    caching/        # Tool result cache
    config/         # Runtime, policy, command, sandbox, approval configs
    indexing/       # Trigram file index, semantic index, speculative prefetch
    interfaces/     # Abstract interfaces (IModelClient, IFileSystem, ICommandRunner, ...)
    mcp/            # MCP protocol server (JSON-RPC 2.0)
    model/          # ChatMessage, ToolSchema, TokenUsage
    net/            # SSE parser
    routing/        # Model router, prompt compressor
    session/        # Session persistence, persistent memory
    threading/      # Thread pool
  platform/
    linux/          # libcurl, fork/exec, /proc, bubblewrap sandbox
    windows/        # WinHTTP, UI Automation, CreateProcess
  providers/        # OpenAI, Claude, Gemini, Ollama, Groq clients
web/                # Browser UI (dark/light theme, Markdown, code highlight, SSE)
vscode-extension/   # VS Code sidebar chat + commands
npm/                # npm package wrapper (bolt-agent)
tests/              # 7 test executables, 147 total tests
third_party/        # nlohmann/json (header-only)
```

---

## MCP Protocol (Claude Code / Cursor Integration)

Add to your MCP client config:

```json
{
  "mcpServers": {
    "bolt": {
      "command": "bolt",
      "args": ["mcp-server"]
    }
  }
}
```

All 23 built-in tools + any loaded plugins are exposed via JSON-RPC 2.0 over stdin/stdout.

---

## VS Code Extension

```bash
cd vscode-extension
npm install && npm run compile
# F5 in VS Code to launch, or:
npx vsce package  # creates .vsix
```

**Features:** sidebar chat panel, right-click "Explain Selection" / "Fix Selection" / "Generate Tests", configurable provider and model.

---

## Testing

```bash
./build/kernel_tests              # 70 unit tests
./build/agent_integration_tests   # 3 integration tests
./build/capability_tests          # 41 capability tests
./build/e2e_tests                 # 9 E2E tests
./build/sse_parser_tests          # 10 SSE parser tests
./build/mcp_server_tests          # 8 MCP server tests
./build/approval_provider_tests   # 6 approval provider tests
```

Run all (147 tests):
```bash
./build/kernel_tests && ./build/agent_integration_tests && \
./build/capability_tests && ./build/e2e_tests && \
./build/sse_parser_tests && ./build/mcp_server_tests && \
./build/approval_provider_tests
```

---

## Contributing

Contributions welcome! See [CONTRIBUTING.md](CONTRIBUTING.md) for guidelines.

```bash
cmake -B build -S .
cmake --build build -j$(nproc)
# Run all tests before submitting a PR
./build/kernel_tests && ./build/agent_integration_tests && ./build/capability_tests
```

**Areas we need help with:**
- **Tree-sitter AST** -- replace regex code intelligence with proper parsing
- **More providers** -- Mistral, Cohere, local GGUF inference
- **RAG / vector search** -- semantic code retrieval for large codebases
- **VS Code marketplace** -- publish the extension

---

## Roadmap

- [x] Multi-provider LLM support (Ollama, OpenAI, Claude, Gemini, Groq)
- [x] Autonomous edit -> build -> test -> fix loop (auto-verify)
- [x] Code intelligence (definitions, references, classes)
- [x] Task planning and progress tracking
- [x] Thread pool parallel tool execution
- [x] Trigram file index + speculative prefetch
- [x] SSE streaming Web UI with dark/light theme
- [x] Cross-platform (Windows, Linux, macOS)
- [x] GitHub Actions CI/CD
- [x] MCP protocol server
- [x] Plugin system (JSON subprocess)
- [x] SKILL.md system with frontmatter
- [x] Session persistence + persistent memory
- [x] VS Code extension
- [x] Docker image (multi-stage, with sandbox)
- [x] npm package (bolt-agent)
- [x] HTTPS support (libcurl)
- [x] Sandbox (Bubblewrap + Seatbelt)
- [x] Speculative tool execution during streaming
- [x] Agent Team mode (parallel workers with git worktrees)
- [x] Setup wizard (first-run provider/model selection)
- [x] REST API server
- [x] Telegram bot gateway
- [x] Discord bot gateway
- [x] Web search + web fetch tools
- [x] Headless browser tool (Chrome)
- [x] File audit logging
- [x] Model router (fast/strong provider split)
- [ ] Pre-built release binaries
- [ ] Tree-sitter AST integration
- [ ] RAG / vector search
- [ ] VS Code marketplace publish

---

## License

MIT
