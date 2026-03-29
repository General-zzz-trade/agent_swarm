# Agent Swarm

A blazing-fast autonomous coding agent written in C++. Single binary, zero dependencies, 0.1ms framework overhead.

> Think of it as a local Claude Code / Cursor / Aider alternative that runs as a single native executable.

## Why C++?

| | Python agents | Agent Swarm |
|---|---|---|
| **Startup** | 2-5 seconds | **<50ms** |
| **Framework overhead** | 10-50ms/turn | **0.1ms/turn** |
| **Deployment** | Python + pip + venv | **Single .exe** |
| **Memory** | 200-500MB | **<30MB** |
| **Parallel tools** | GIL-limited | **True multi-core** |
| **Streaming latency** | Buffered | **Zero-copy SSE** |

## Features

- **19 built-in tools**: file ops, code search, code intelligence, build & test, task planning, git, shell commands, desktop automation
- **5 LLM providers**: Ollama (local), OpenAI, Claude, Gemini, Groq + any OpenAI-compatible API
- **Autonomous loop**: edit -> compile -> test -> fix -> repeat until passing
- **Code intelligence**: find definitions, references, classes across C++/Python/JS/Rust/Go
- **Task planner**: decompose complex tasks into tracked steps
- **Performance engine**: thread pool, trigram file index, speculative prefetch, HTTP/2 multiplexing
- **Web UI**: browser-based chat with real-time SSE streaming
- **Desktop automation** (Windows): control windows, click UI elements, type text

## Quick Start

### Option 1: Build from source

```bash
git clone https://github.com/General-zzz-trade/agent_swarm.git
cd agent_swarm
cmake -B build -S .
cmake --build build -j8
./build/mini_nn agent "Hello, what tools do you have?"
```

**Requirements**: C++17 compiler (GCC 9+, Clang 10+, MSVC 2019+), CMake 3.10+

### Option 2: With Ollama (local AI, no API key needed)

```bash
# Install Ollama: https://ollama.ai
ollama pull qwen3:8b

# Run agent with local model
./build/mini_nn agent "Read CMakeLists.txt and list all build targets"
```

## Usage

### CLI modes

```bash
# Single-turn: ask a question, get an answer
mini_nn agent "Search the codebase for all TODO comments"

# Interactive mode
mini_nn agent
> Read src/main.cpp
> Edit it to add error handling
> Run build_and_test to verify

# Web UI (browser-based chat)
mini_nn web-chat --port 8080

# Performance benchmark
mini_nn bench --rounds 5
```

### Configure LLM provider

Create `mini_nn_cpp.conf` in your project root:

```ini
# Local model (default)
provider = ollama-chat

# Or use cloud APIs
# provider = openai
# provider = claude
# provider = gemini
# provider = groq

# Auto-approve all tool calls (for autonomous mode)
approval.mode = auto-approve
```

Set API keys as environment variables:
```bash
export OPENAI_API_KEY=sk-...
export ANTHROPIC_API_KEY=sk-ant-...
export GEMINI_API_KEY=AI...
export GROQ_API_KEY=gsk_...
```

### Available tools

| Tool | Description | Auto-approved |
|------|-------------|:---:|
| `read_file` | Read file contents | Yes |
| `list_dir` | List directory | Yes |
| `search_code` | Text search across workspace | Yes |
| `code_intel` | Find definitions, references, classes | Yes |
| `calculator` | Arithmetic expressions | Yes |
| `edit_file` | Modify existing files | Needs approval |
| `write_file` | Create new files | Needs approval |
| `build_and_test` | Compile + run tests | Needs approval |
| `run_command` | Shell commands (git, cmake, etc.) | Needs approval |
| `task_planner` | Create and track multi-step plans | Yes |
| `list_processes` | List running processes | Yes |
| `list_windows` | List visible windows | Yes |
| `open_app` | Launch applications | Needs approval |
| `focus_window` | Bring window to foreground | Needs approval |
| `inspect_ui` | Inspect UI element tree | Yes |
| `click_element` | Click UI elements | Needs approval |
| `type_text` | Type text into focused window | Needs approval |

## Architecture

```
                    +-----------------+
                    |   LLM Provider  |
                    | (Ollama/OpenAI/ |
                    |  Claude/Gemini) |
                    +--------+--------+
                             |
                    +--------v--------+
                    |   Agent Loop    |
                    | (50 steps max)  |
                    +--------+--------+
                             |
              +--------------+--------------+
              |              |              |
     +--------v---+  +------v------+  +----v-------+
     | Tool Engine |  | Thread Pool |  | File Index |
     | (19 tools)  |  | (parallel)  |  | (trigram)  |
     +--------+----+  +------+------+  +----+-------+
              |              |              |
     +--------v--------------v--------------v--------+
     |              Workspace (your code)             |
     +------------------------------------------------+
```

## Benchmark

```
=== Framework Overhead (no network) ===
  Agent loop:              0.1ms
  JSON serialize (10 tools): 0.2ms
  Tool lookup (1000x):     0.1ms (O(1) hash)
  8 tools parallel:        1.2ms vs sequential 4.0ms (3.4x speedup)
  File index build:        173ms (178 files, 1.5MB)
  Prefetch cache:          593x faster than disk

=== Ollama qwen3:8b (local) ===
  TTFT:                    164ms
  Token throughput:        14.9 tok/s
  Streaming overhead:      +0.8%
```

Run your own benchmark:
```bash
mini_nn bench --provider ollama-chat --rounds 5
mini_nn bench --json > results.json
```

## Project Structure

```
src/
  agent/          # Agent loop, tools, task runner
  app/            # CLI, web server, config, benchmark
  core/
    caching/      # Tool result cache
    config/       # Runtime, policy, command configs
    indexing/     # Trigram file index, prefetch cache
    interfaces/   # Abstract interfaces (IModelClient, IFileSystem, ...)
    model/        # ChatMessage, ToolSchema
    net/          # SSE parser
    routing/      # Model router, prompt compressor
    threading/    # Thread pool
  platform/
    windows/      # WinHTTP, UI automation, process management
  providers/      # OpenAI, Claude, Gemini, Ollama clients
tests/            # 120 tests (kernel + integration + capability)
third_party/      # nlohmann/json
```

## Contributing

Contributions welcome! Areas we need help with:

- **Linux/macOS support**: Abstract Windows-specific APIs (WinHTTP, Winsock, UI Automation)
- **More LLM providers**: Mistral, Cohere, local GGUF inference
- **Tree-sitter integration**: Replace regex-based code intelligence with proper AST parsing
- **MCP protocol**: Standardized tool interface for interoperability
- **Plugin system**: Dynamic tool loading at runtime
- **Docker image**: One-command deployment

### Build and test

```bash
cmake -B build -S .
cmake --build build -j8
./build/kernel_tests          # 76 unit tests
./build/agent_integration_tests  # 3 integration tests
./build/capability_tests      # 41 capability verification tests
```

## Roadmap

- [x] Multi-provider LLM support (Ollama, OpenAI, Claude, Gemini, Groq)
- [x] Autonomous edit -> build -> test -> fix loop
- [x] Code intelligence (definitions, references, classes)
- [x] Task planning and progress tracking
- [x] Performance benchmark suite
- [x] SSE streaming web UI
- [x] Thread pool parallel tool execution
- [x] Trigram file index
- [ ] Cross-platform (Linux, macOS)
- [ ] Pre-built release binaries
- [ ] GitHub Actions CI/CD
- [ ] Plugin/extension system
- [ ] MCP protocol support
- [ ] Tree-sitter AST integration
- [ ] Docker image
- [ ] VS Code extension

## License

MIT
