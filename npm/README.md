# agent-swarm-cpp

Blazing-fast autonomous coding agent written in C++. Single binary, zero dependencies, 0.1ms framework overhead.

## Install

```bash
npm install -g agent-swarm-cpp
```

## Usage

```bash
# Single question
agent-swarm agent "Read src/main.cpp and explain what it does"

# Interactive mode
agent-swarm agent

# Web UI
agent-swarm web-chat --port 8080

# Performance benchmark
agent-swarm bench --rounds 5
```

## With local AI (no API key needed)

```bash
# Install Ollama: https://ollama.ai
ollama pull qwen3:8b
agent-swarm agent "Search the codebase for TODO comments"
```

## With cloud APIs

```bash
export OPENAI_API_KEY=sk-...
agent-swarm agent "Refactor the authentication module"
```

## Features

- 19 built-in tools (file ops, code search, build & test, git, desktop automation)
- 5 LLM providers (Ollama, OpenAI, Claude, Gemini, Groq)
- Autonomous edit -> build -> test -> fix loop
- 0.1ms framework overhead per agent turn
- Single native binary, no Python/Node runtime needed

## More info

GitHub: https://github.com/General-zzz-trade/agent_swarm
