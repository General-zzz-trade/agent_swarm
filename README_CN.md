# Bolt

<!-- Badges placeholder -->
<!--
[![Build](https://github.com/General-zzz-trade/Bolt/actions/workflows/ci.yml/badge.svg)](https://github.com/General-zzz-trade/Bolt/actions)
[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
[![npm](https://img.shields.io/npm/v/bolt-agent)](https://www.npmjs.com/package/bolt-agent)
-->

极速自主编码智能体，纯 C++17 实现。单文件部署，零依赖，0.2ms 框架开销。

> 可以理解为本地版的 Claude Code / Cursor / Aider，以单个原生可执行文件运行，无任何运行时依赖。

[English](README.md) | 中文

---

## 为什么用 C++？

| | Python 智能体 | Bolt |
|---|---|---|
| **启动时间** | 2-5 秒 | **<50ms** |
| **框架开销** | 10-50ms/轮 | **0.2ms/轮** |
| **部署方式** | Python + pip + venv | **单个二进制文件** |
| **内存占用** | 200-500MB | **<30MB** |
| **并行工具** | 受 GIL 限制 | **真正多核并行** |
| **流式延迟** | 缓冲输出 | **零拷贝 SSE** |
| **沙箱隔离** | 无内置方案 | **Bubblewrap / Seatbelt** |
| **投机执行** | 不支持 | **流式输出期间预执行只读工具** |

---

## 功能特性

- **27 个内置工具** -- 文件操作、代码搜索、代码智能、编译测试、Git、Shell 命令、网页抓取、网络搜索、无头浏览器、任务规划、桌面自动化、邮件发送、浏览器自动化
- **插件系统** -- 用任何语言（Python、Node、Go、Rust 等）编写扩展工具
- **SKILL.md 系统** -- 可复用的提示模板，支持 YAML 前置元数据和自动加载
- **11 个 LLM 提供商** -- Ollama（本地）、OpenAI、Claude、Gemini、Groq、DeepSeek、通义千问、智谱 GLM、Moonshot、百川、豆包 + 模型路由 + 任何 OpenAI 兼容 API
- **10 种运行模式** -- 交互终端、单轮对话、管道模式、Web 界面、REST API、MCP 服务器、Telegram 机器人、Discord 机器人、微信机器人、Slack 机器人、性能基准
- **自主循环** -- 编辑 → 编译 → 测试 → 修复 → 重复（自动验证，最多重试 3 次）
- **多 Agent 协作** -- 并行多智能体执行，基于 git worktree 隔离
- **投机执行** -- 在 LLM 流式输出完成前，只读工具即开始运行
- **沙箱隔离** -- Bubblewrap（Linux）和 Seatbelt（macOS）提供操作系统级别的命令隔离
- **持久化记忆** -- 跨会话事实存储于 `~/.bolt/memory.json`
- **会话持久化** -- 保存/恢复对话，使用 `--resume` 恢复上次会话
- **代码智能** -- 跨 C++/Python/JS/Rust/Go 查找定义、引用、类
- **首次启动向导** -- 交互式引导选择提供商和模型
- **VS Code 扩展** -- 侧边栏聊天、解释/修复选中代码、生成测试
- **Web 界面** -- 深色/浅色主题、Markdown 渲染、代码高亮、SSE 流式输出
- **性能引擎** -- 线程池、三元组索引、投机预取、HTTP/2、连接池
- **审计日志** -- 所有写操作记录到文件，便于安全审查
- **跨平台** -- Linux、macOS、Windows
- **Docker** -- 多阶段构建，内置沙箱

---

## 快速开始

### npm 安装

```bash
npm install -g bolt-agent
bolt agent "你好，你有什么工具？"
```

### 从源码编译

```bash
git clone https://github.com/General-zzz-trade/Bolt.git
cd Bolt
cmake -B build -S .
cmake --build build -j$(nproc)
./build/bolt agent "读取 src/main.cpp 并解释它"
```

**环境要求**：C++17 编译器（GCC 9+、Clang 10+、MSVC 2019+）、CMake 3.10+、libcurl（Linux/macOS）

### 搭配 Ollama（本地 AI，无需 API Key）

```bash
# 安装 Ollama：https://ollama.ai
ollama pull qwen3:8b
bolt agent "读取 CMakeLists.txt 并列出所有构建目标"
```

### Docker

```bash
docker build -t bolt .
docker run -v $(pwd):/workspace bolt agent "列出文件"
```

Docker 镜像采用多阶段构建，运行镜像包含 libcurl、bubblewrap（沙箱）、git 和 CA 证书。

---

## 运行模式

```bash
# 交互终端（默认）-- readline、Markdown 渲染、@file 引用
bolt

# 单轮对话
bolt agent "搜索代码库中的 TODO 注释"

# 管道模式（接收 stdin 输入）
bolt agent -p

# 恢复上次会话
bolt agent --resume

# Web 界面（深色/浅色主题，SSE 流式输出）
bolt web-chat --port 8080

# REST API 服务器
bolt api-server --port 9090

# MCP 服务器（集成 Claude Code / Cursor）
bolt mcp-server

# Telegram 机器人
TELEGRAM_BOT_TOKEN=... bolt telegram

# Discord 机器人
DISCORD_BOT_TOKEN=... DISCORD_CHANNEL_ID=... bolt discord

# 微信机器人
WECHAT_BOT_TOKEN=... bolt wechat

# Slack 机器人
SLACK_BOT_TOKEN=... SLACK_APP_TOKEN=... bolt slack

# 性能基准测试
bolt bench --rounds 5
bolt bench --json > results.json
```

---

## 首次启动向导

首次运行时，Bolt 会启动交互式设置向导，引导你完成以下配置：

1. **选择提供商** -- Ollama（本地）、OpenAI、Claude、Gemini、Groq、DeepSeek、通义千问、智谱 GLM、Moonshot、百川、豆包
2. **选择模型** -- 从所选提供商的可用模型中挑选
3. **输入 API Key** -- 保存至 `~/.bolt/config.json`

随时可以通过交互命令 `/model` 重新选择模型。

---

## 内置工具

### 文件与代码

| 工具 | 功能 | 只读 |
|------|------|:----:|
| `read_file` | 读取文件内容（最大 32KB） | 是 |
| `list_dir` | 列出目录内容 | 是 |
| `write_file` | 创建新文件 | 否 |
| `edit_file` | 修改现有文件（精确文本替换） | 否 |
| `delete_file` | 删除文件 | 否 |
| `search_code` | 全文搜索工作区 | 是 |
| `code_intel` | 查找定义、引用、类、包含关系 | 是 |

### 构建与运行

| 工具 | 功能 | 只读 |
|------|------|:----:|
| `build_and_test` | 自动检测构建系统、编译、运行测试 | 否 |
| `run_command` | Shell 命令（35+ 白名单工具，完整 Git） | 否 |
| `git` | Git 操作（status、diff、log、commit 等） | 是 |
| `calculator` | 算术表达式计算 | 是 |
| `task_planner` | 创建和跟踪多步任务计划 | 是 |

### 网络与通信

| 工具 | 功能 | 只读 |
|------|------|:----:|
| `web_fetch` | 抓取 URL 并提取文本内容 | 是 |
| `web_search` | 通过 DuckDuckGo 进行网络搜索 | 是 |
| `browser` | 无头 Chrome：导航、截图、点击、提取文本 | 是 |
| `send_email` | 通过 SMTP 发送邮件（支持附件） | 否 |

### 桌面自动化

| 工具 | 功能 | 只读 |
|------|------|:----:|
| `list_processes` | 列出运行中的进程 | 是 |
| `open_app` | 启动应用程序 | 否 |
| `list_windows` | 列出可见窗口 | 是 |
| `focus_window` | 将窗口置前 | 否 |
| `wait_for_window` | 等待窗口出现 | 是 |
| `inspect_ui` | 检查 UI 元素树 | 是 |
| `click_element` | 点击 UI 元素 | 否 |
| `type_text` | 向聚焦窗口输入文本 | 否 |

---

## 插件系统

用任何语言编写扩展工具，插件通过 stdin/stdout 以 JSON 格式通信。

**目录结构：**
```
.bolt/plugins/my-plugin/     # 工作区本地插件
~/.bolt/plugins/my-plugin/   # 全局插件
  plugin.json                # 清单文件（必需）
  my_tool.py                 # 可执行脚本
```

**plugin.json 清单：**
```json
{
  "name": "my-plugin",
  "version": "1.0.0",
  "tools": [
    {
      "name": "my_tool",
      "description": "做些有用的事",
      "command": "python3 my_tool.py"
    }
  ]
}
```

**插件示例（Python）：**
```python
#!/usr/bin/env python3
import json, sys
req = json.loads(input())
if req["method"] == "describe":
    print(json.dumps({"name": "my_tool", "description": "我的自定义工具"}))
elif req["method"] == "run":
    result = f"收到参数: {req['params']['args']}"
    print(json.dumps({"success": True, "result": result}))
```

插件工具继承沙箱限制，与内置工具一同展示。

---

## SKILL.md 系统

技能是以 Markdown 文件存储的可复用提示模板，支持 YAML 前置元数据。

```markdown
---
name: Code Review
description: 代码审查准则
auto_load: true
---

审查代码时，注意检查：
1. 错误处理
2. 边界情况
3. 性能影响
...
```

将 `SKILL.md` 文件放在 `.bolt/skills/`（工作区）或 `~/.bolt/skills/`（全局）目录下。设置 `auto_load: true` 的技能会自动注入每次提示。

---

## 沙箱隔离

Bolt 对所有 Shell 命令进行沙箱隔离，提供操作系统级别的安全保护。

| 平台 | 技术 | 机制 |
|------|------|------|
| Linux | [Bubblewrap](https://github.com/containers/bubblewrap) (`bwrap`) | 挂载命名空间、seccomp |
| macOS | Seatbelt (`sandbox-exec`) | macOS 沙箱配置文件 |

**配置**（`bolt.conf`）：
```ini
sandbox.enabled = true
sandbox.network = true
sandbox.allow_write = /tmp, /var/tmp
sandbox.deny_read = ~/.ssh, ~/.aws, ~/.gnupg
```

**环境变量覆盖：**
```bash
export BOLT_SANDBOX_ENABLED=true
export BOLT_SANDBOX_NETWORK=false
```

若系统未安装 bwrap/seatbelt，将回退到非沙箱执行。

---

## 性能

```
=== 框架开销（无网络） ===
  Agent 循环:               0.2ms
  JSON 序列化 (10 工具):    0.2ms
  工具查找 (1000次):        0.1ms  (O(1) 哈希)
  8 工具并行 (线程池):      1.2ms  对比串行 4.0ms  (3.4x 加速)
  文件索引构建:             173ms  (178 文件, 1.5MB)
  预取缓存:                 593x 快于磁盘读取
  投机执行:                 流式输出期间预执行只读工具

=== Ollama qwen3:8b (本地) ===
  冷启动连接:               236ms
  首字延迟:                 164ms
  生成吞吐:                 14.9 tok/s
```

**核心优化：**
- **投机执行器** -- 检测流式输出中的工具调用模式，预执行只读工具
- **HTTP/2 + 连接池** -- 与 LLM 提供商保持长连接
- **三元组文件索引** -- 亚毫秒级代码搜索
- **线程池** -- 真正多核并行的工具执行
- **提示压缩器** -- 通过模型路由（快速/强力提供商分流）缩减上下文

---

## 安全

| 层级 | 机制 |
|------|------|
| **操作系统沙箱** | Bubblewrap（Linux）/ Seatbelt（macOS）隔离所有 Shell 命令 |
| **权限策略** | 工具分为只读和写入两类；写入工具需要审批 |
| **命令白名单** | 35+ 预批准命令；其他命令需明确审批 |
| **审计日志** | 所有写操作记录到 `.bolt/audit.log` |
| **敏感路径拦截** | 默认屏蔽 `~/.ssh`、`~/.aws`、`~/.gnupg`、`/etc/shadow` |
| **网络管控** | 可选的出站请求域名白名单 |
| **审批模式** | `auto-approve`（自动）、`ask`（交互询问）或自定义策略 |

---

## 交互功能

### 斜杠命令

| 类别 | 命令 |
|------|------|
| **会话** | `/save [name]`  `/load <id>`  `/sessions`  `/delete <id>`  `/export [file]`  `/memory` |
| **上下文** | `/clear`  `/compact`  `/undo`  `/reset`  `/context` |
| **显示** | `/model`  `/cost`  `/status`  `/debug`  `/diff`  `/doctor` |
| **模式** | `/plan`  `/auto` |
| **系统** | `/quit`  `/help`  `/init`  `/sandbox`  `/plugins`  `/skills`  `/team`  `/bench` |

### 快捷键

| 快捷键 | 功能 |
|--------|------|
| `Ctrl+C` | 取消当前操作 |
| `Ctrl+L` | 清屏 |
| `Ctrl+D` | 退出 |
| `Up/Down` | 历史命令 |
| `Tab` | 自动补全 |

### @file 引用

在提示中直接引用文件内容：

```
> 解释 @src/main.cpp
> @src/agent/tool.h:10-30 是做什么的？
```

---

## 国产大模型接入

Bolt 内置了 6 家国产大模型的原生提供商支持，开箱即用。也可通过 OpenAI 兼容 API 接入其他服务。

### DeepSeek

```ini
provider = deepseek
deepseek.model = deepseek-chat
```

```bash
export DEEPSEEK_API_KEY=sk-...
```

### 通义千问（Qwen）

```ini
provider = qwen
qwen.model = qwen-plus
```

```bash
export QWEN_API_KEY=sk-...     # 阿里云 DashScope API Key
```

### 智谱 GLM

```ini
provider = glm
glm.model = glm-4
```

```bash
export GLM_API_KEY=...
```

### Moonshot（月之暗面）

```ini
provider = moonshot
moonshot.model = moonshot-v1-8k
```

```bash
export MOONSHOT_API_KEY=sk-...
```

### 百川智能

```ini
provider = baichuan
baichuan.model = Baichuan4
```

```bash
export BAICHUAN_API_KEY=sk-...
```

### 豆包（字节跳动）

```ini
provider = doubao
doubao.model = <你的接入点 ID>
```

```bash
export DOUBAO_API_KEY=...      # 火山引擎 API Key
```

> 除原生支持的 6 家国产提供商外，其他兼容 OpenAI 格式的大模型服务也可通过设置 `openai.base_url` 接入。

---

## 配置

### bolt.conf

在项目根目录创建 `bolt.conf`：

```ini
# 提供商: ollama | ollama-chat | openai | claude | gemini | groq | deepseek | qwen | glm | moonshot | baichuan | doubao | router
provider = ollama-chat

# 模型
openai.model = gpt-4o
claude.model = claude-sonnet-4-20250514
gemini.model = gemini-2.0-flash

# 路由模式：简单任务用快速模型，复杂任务用强力模型
router.fast_provider = groq
router.strong_provider = claude

# 审批
approval.mode = auto-approve

# 沙箱
sandbox.enabled = true
sandbox.network = true

# Agent 行为
agent.auto_verify = true
agent.max_retries = 3
```

### 环境变量

```bash
export OPENAI_API_KEY=sk-...
export ANTHROPIC_API_KEY=sk-ant-...
export GEMINI_API_KEY=AI...
export GROQ_API_KEY=gsk_...
export DEEPSEEK_API_KEY=sk-...
export QWEN_API_KEY=sk-...
export GLM_API_KEY=...
export MOONSHOT_API_KEY=sk-...
export BAICHUAN_API_KEY=sk-...
export DOUBAO_API_KEY=...
export TELEGRAM_BOT_TOKEN=...
export DISCORD_BOT_TOKEN=...
export DISCORD_CHANNEL_ID=...
export WECHAT_BOT_TOKEN=...
export SLACK_BOT_TOKEN=xoxb-...
export SLACK_APP_TOKEN=xapp-...
export BOLT_SANDBOX_ENABLED=true
export BOLT_PROVIDER=claude
```

---

## 架构

```
                         +-----------------+
                         |   LLM 提供商    |
                         | (Ollama/OpenAI/ |
                         |Claude/Gemini/   |
                         |Groq/DeepSeek/   |
                         |Qwen/GLM/路由)   |
                         +--------+--------+
                                  |
                    +-------------v--------------+
                    |        Agent 循环          |     0.2ms 开销
                    | (最多 50 步, 自动验证)      |
                    +-------------+--------------+
                                  |
          +-----------+-----------+-----------+-----------+
          |           |           |           |           |
   +------v---+ +----v-----+ +--v------+ +--v------+ +--v--------+
   | 工具引擎 | |  投机    | | 线程池 | | 文件    | | 集群      |
   | (27 工具)| |  执行器  | | (N核)  | | 索引    | | 协调器    |
   | + 插件   | | (流式)   | |        | | (三元组)| | (团队模式)|
   +------+---+ +----------+ +--------+ +---------+ +-----------+
          |
   +------v---------------------------------------------------+
   |                   工作区（你的代码）                        |
   +----+------------+------------+------------+---------------+
        |            |            |            |
   +----v----+ +----v----+ +----v----+ +-----v------+
   |MCP 服务器| | 插件   | | 会话    | | 记忆存储   |
   |(JSON-RPC)| | 系统   | | 存储    | | (跨会话)   |
   +---------+ +---------+ +---------+ +------------+
        |            |
   +----v----+ +----v--------+
   |  沙箱   | | 审计日志    |
   | (bwrap/ | | (文件记录)  |
   |seatbelt)| +-------------+
   +---------+

   前端:
   +----------+ +--------+ +--------+ +---------+ +---------+ +--------+ +-------+
   |  终端    | |Web 界面| |REST API| |Telegram | |Discord  | | 微信   | | Slack |
   |(readline)| | (SSE)  | | 服务器 | | 机器人  | | 机器人  | | 机器人 | | 机器人|
   +----------+ +--------+ +--------+ +---------+ +---------+ +--------+ +-------+
```

---

## 项目结构

```
src/
  agent/            # Agent 循环、27 个工具、插件、技能、投机执行器、集群
  app/              # CLI、Web 服务器、API 服务器、Telegram/Discord/微信/Slack 机器人、设置向导
  core/
    caching/        # 工具结果缓存
    config/         # 运行时、策略、命令、沙箱、审批配置
    indexing/       # 三元组文件索引、语义索引、投机预取
    interfaces/     # 抽象接口 (IModelClient, IFileSystem, ICommandRunner, ...)
    mcp/            # MCP 协议服务器 (JSON-RPC 2.0)
    model/          # ChatMessage, ToolSchema, TokenUsage
    net/            # SSE 解析器
    routing/        # 模型路由器、提示压缩器
    session/        # 会话持久化、持久记忆
    threading/      # 线程池
  platform/
    linux/          # libcurl、fork/exec、/proc、bubblewrap 沙箱
    windows/        # WinHTTP、UI 自动化、CreateProcess
  providers/        # OpenAI、Claude、Gemini、Ollama、Groq、DeepSeek、Qwen、GLM、
                    #   Moonshot、Baichuan、Doubao 客户端
web/                # 浏览器 UI（深色/浅色主题、Markdown、代码高亮、SSE）
vscode-extension/   # VS Code 侧边栏聊天 + 命令
npm/                # npm 包装器 (bolt-agent)
tests/              # 8 个测试程序，共 159 个测试
third_party/        # nlohmann/json（仅头文件）
```

---

## MCP 协议（Claude Code / Cursor 集成）

添加到 MCP 客户端配置：

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

全部 27 个内置工具及已加载插件均通过 JSON-RPC 2.0 协议经由 stdin/stdout 暴露。

---

## VS Code 扩展

```bash
cd vscode-extension
npm install && npm run compile
# 在 VS Code 中按 F5 启动，或：
npx vsce package  # 生成 .vsix 文件
```

**功能：** 侧边栏聊天面板、右键"解释选中代码"/"修复选中代码"/"生成测试"、可配置提供商和模型。

---

## 测试

```bash
./build/kernel_tests              # 70 个单元测试
./build/agent_integration_tests   # 3 个集成测试
./build/capability_tests          # 41 个能力验证测试
./build/e2e_tests                 # 9 个端到端测试
./build/sse_parser_tests          # 10 个 SSE 解析器测试
./build/mcp_server_tests          # 8 个 MCP 服务器测试
./build/approval_provider_tests   # 6 个审批提供商测试
./build/api_e2e_tests             # 12 个 API 端到端测试
```

运行全部（共 159 个测试）：
```bash
./build/kernel_tests && ./build/agent_integration_tests && \
./build/capability_tests && ./build/e2e_tests && \
./build/sse_parser_tests && ./build/mcp_server_tests && \
./build/approval_provider_tests && ./build/api_e2e_tests
```

---

## 参与贡献

欢迎贡献！详见 [CONTRIBUTING.md](CONTRIBUTING.md)。

```bash
cmake -B build -S .
cmake --build build -j$(nproc)
# 提交 PR 前请运行全部测试
./build/kernel_tests && ./build/agent_integration_tests && ./build/capability_tests
```

**我们需要帮助的方向：**
- **Tree-sitter AST** -- 用正式的 AST 解析替代 regex 代码智能
- **RAG / 向量搜索** -- 大型代码库的语义检索
- **VS Code 市场** -- 发布扩展

---

## 路线图

- [x] 多提供商 LLM 支持 (Ollama, OpenAI, Claude, Gemini, Groq)
- [x] 国产大模型原生支持 (DeepSeek, 通义千问, 智谱 GLM, Moonshot, 百川, 豆包)
- [x] 自主 编辑 → 编译 → 测试 → 修复 循环（自动验证）
- [x] 代码智能（定义、引用、类）
- [x] 任务规划和进度跟踪
- [x] 线程池并行工具执行
- [x] 三元组文件索引 + 投机预取
- [x] SSE 流式 Web UI（深色/浅色主题）
- [x] 跨平台 (Windows, Linux, macOS)
- [x] GitHub Actions CI/CD
- [x] MCP 协议服务器
- [x] 插件系统（JSON 子进程）
- [x] SKILL.md 系统（YAML 前置元数据）
- [x] 会话持久化 + 持久记忆
- [x] VS Code 扩展
- [x] Docker 镜像（多阶段构建，内置沙箱）
- [x] npm 包 (bolt-agent)
- [x] HTTPS 支持 (libcurl)
- [x] 沙箱隔离 (Bubblewrap + Seatbelt)
- [x] 流式输出期间投机执行工具
- [x] 多 Agent 协作模式（并行 worker + git worktree）
- [x] 首次启动向导（提供商/模型选择）
- [x] REST API 服务器
- [x] Telegram 机器人
- [x] Discord 机器人
- [x] 微信机器人
- [x] Slack 机器人
- [x] 网络搜索 + 网页抓取工具
- [x] 无头浏览器工具 (Chrome)
- [x] 邮件发送工具 (SMTP)
- [x] 文件审计日志
- [x] 模型路由器（快速/强力提供商分流）
- [x] 27 个斜杠命令（/plan、/auto、/doctor、/context、/init、/skills、/team 等）
- [x] API 端到端测试 (api_e2e_tests)
- [ ] 预编译发布二进制
- [ ] Tree-sitter AST 集成
- [ ] RAG / 向量搜索
- [ ] VS Code 市场发布

---

## 许可证

MIT
