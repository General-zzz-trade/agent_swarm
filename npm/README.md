# bolt-agent

Blazing-fast autonomous coding agent written in C++. Single binary, zero dependencies, 0.1ms framework overhead.

## Install

```bash
npm install -g bolt-agent
```

## Usage

```bash
# Single question
bolt agent "Read src/main.cpp and explain what it does"

# Interactive mode
bolt agent

# Web UI
bolt web-chat --port 8080

# Performance benchmark
bolt bench --rounds 5
```

## With local AI (no API key needed)

```bash
# Install Ollama: https://ollama.ai
ollama pull qwen3:8b
bolt agent "Search the codebase for TODO comments"
```

## With cloud APIs

```bash
export OPENAI_API_KEY=sk-...
bolt agent "Refactor the authentication module"
```

## Features

- 19 built-in tools (file ops, code search, build & test, git, desktop automation)
- 5 LLM providers (Ollama, OpenAI, Claude, Gemini, Groq)
- Autonomous edit -> build -> test -> fix loop
- 0.1ms framework overhead per agent turn
- Single native binary, no Python/Node runtime needed

## More info

GitHub: https://github.com/General-zzz-trade/bolt
