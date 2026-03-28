# mini_nn_cpp

这个项目已经从单一的矩阵/训练示例，重构成了一个更适合继续扩展的 C++ 学习项目：

- `train-demo` 子命令保留原来的线性回归训练示例
- `agent` 子命令提供一个最小可运行的本地智能体骨架
- `web-chat` 子命令提供一个本地网页对话界面，用来验证 Agent 当前能力

## 当前结构

```text
src/
  app/
    app_config.cpp
    agent_factory.cpp
    agent_cli_options.cpp
    agent_services.h
    agent_runner.cpp
    approval_provider_factory.cpp
    file_audit_logger.cpp
    program_cli.cpp
    static_approval_provider.cpp
    terminal_approval_provider.cpp
    web_approval_provider.cpp
    web_chat_cli_options.cpp
    web_chat_server.cpp
  core/
    config/
      agent_runtime_config.h
      approval_config.h
      command_policy_config.h
      ollama_connection_config.h
      policy_config.h
    interfaces/
      approval_provider.h
      command_runner.h
      file_system.h
      model_client.h
  platform/
    platform_agent_factory.cpp
    linux/
      linux_agent_factory.cpp
    windows/
      windows_agent_factory.cpp
      ollama_json_utils.cpp
      windows_command_runner.cpp
      windows_file_system.cpp
      windows_ollama_model_client.cpp
      windows_process_manager.cpp
      windows_window_controller.cpp
  agent/
    action_parser.cpp
    agent.cpp
    tool_set_factory.cpp
    tool_registry.cpp
    edit_file_tool.cpp
    focus_window_tool.cpp
    list_dir_tool.cpp
    list_processes_tool.cpp
    list_windows_tool.cpp
    open_app_tool.cpp
    permission_policy.cpp
    calculator_tool.cpp
    read_file_tool.cpp
    run_command_tool.cpp
    search_code_tool.cpp
    write_file_tool.cpp
  demo/
    training_demo.cpp
  matrix.cpp
  main.cpp
web/
  index.html
  app.js
  styles.css
```

## 现在能做什么

### 1. 训练示例

保留了最小训练循环，用来学习：

- 前向传播
- 均方误差
- 梯度计算
- 参数更新

运行方式：

```powershell
.\build\mini_nn.exe train-demo
```

### 2. 本地 Agent

最小 Agent 具备这些模块：

- `ActionParser`
  把模型返回的单个 JSON 对象解析成 `reply` 或 `tool` 动作
- `IModelClient / ICommandRunner / IFileSystem`
  Core 层只依赖接口，不直接依赖具体平台实现
- `IApprovalProvider`
  Core 层的审批接口，决定需要确认的动作是否放行
- `AppConfig`
  从工作区 `mini_nn_cpp.conf` 和环境变量加载默认模型、Ollama 连接参数、命令白名单、审批策略和运行时参数
- `AgentFactory / AgentServices`
  统一用平台服务包来创建 `Agent`，避免平台层直接耦合到完整组装流程
- `AgentCliOptions`
  统一处理 CLI 参数解析，以及 CLI 对配置默认值的覆盖
- `AgentRunner`
  统一处理 Agent 的单轮执行和交互循环
- `ProgramCli`
  统一处理顶层命令分发和 usage 文本生成
- `WindowsAgentFactory`
  统一组装 Windows 平台下的文件系统、命令执行器、审批器和模型客户端
- `PlatformAgentFactory`
  统一选择当前平台的服务工厂；当前 Windows 会转发到 `WindowsAgentFactory`，Linux 先接了明确的 stub，后续 macOS 只需要在这一层增加分发
- `LinuxAgentFactory`
  当前 Linux stub，会在非 Windows 构建下返回清晰的“尚未实现”错误，而不是退回泛化异常
- `WindowsOllamaModelClient`
  当前 Windows 适配器，通过 WinHTTP 直接调用本地 `http://127.0.0.1:11434/api/generate`
- `TerminalApprovalProvider`
  当前命令行审批实现，会在写文件、局部编辑和执行命令前显示操作预览并提示确认
- `StaticApprovalProvider`
  用于 `auto-approve / auto-deny` 这类非交互审批模式
- `ApprovalProviderFactory`
  根据 `approval.mode` 选择交互式或静态审批提供器
- `FileAuditLogger`
  把关键副作用动作落到工作区 `.mini_nn/audit.log`，当前覆盖 `write_file`、`edit_file`、`run_command` 的审批和执行链路
- `TaskRunner`
  把工具执行、审批、观察结果收敛成统一的步骤执行器，为后续“计划 -> 执行 -> 观察”状态机做骨架
- `WindowsCommandRunner`
  当前 Windows 适配器，负责执行受限命令
- `WindowsFileSystem`
  当前 Windows 适配器，负责文件读写
- `WindowsProcessManager`
  当前 Windows 适配器，负责列进程和启动本地应用
- `WindowsUiAutomation`
  当前 Windows 适配器，负责向当前前台窗口发送文本输入
- `WindowsWindowController`
  当前 Windows 适配器，负责列出可见窗口和聚焦窗口
- `Agent`
  维护历史消息，决定“直接回答”还是“调用工具”，但自身只依赖接口
- `Tool`
  可扩展工具接口
- `ToolSetFactory`
  负责组装默认工具集合，把具体工具注册从 `Agent` 构造逻辑中抽出来
- `ToolRegistry`
  统一注册和查找工具
- `CalculatorTool`
  计算四则运算和括号表达式
- `EditFileTool`
  在工作区已有文件里做一次或多次精确文本替换，或者按行区间 patch
- `ListDirTool`
  通过 `IFileSystem` 列出工作区内目录内容
- `ReadFileTool`
  读取当前工作区内的文本文件
- `WriteFileTool`
  只在当前工作区内创建或覆盖文本文件
- `RunCommandTool`
  只执行白名单开发命令，比如 `git`、`g++`、`ctest`、`cmake`、`ollama`
- `SearchCodeTool`
  通过 `IFileSystem` 在工作区文本代码文件里做递归字符串搜索
- `ListProcessesTool`
  列出当前运行中的本地进程，可按名称过滤
- `ListWindowsTool`
  列出当前可见的顶层窗口，可按标题或类名过滤
- `OpenAppTool`
  启动本地应用，需要审批
- `FocusWindowTool`
  按窗口标题或句柄聚焦可见窗口，需要审批
- `WaitForWindowTool`
  轮询等待某个窗口出现，适合桌面任务里的过渡步骤
- `InspectUiTool`
  查看当前前台窗口或指定窗口句柄下的可见控件列表
- `ClickElementTool`
  按元素句柄或文本/类名选择器点击控件，需要审批
- `TypeTextTool`
  向当前已聚焦窗口输入文本，需要审批
- `PermissionPolicy`
  在工具真正执行前做风险边界判断；现在工具分组和高风险拦截开关都可以通过配置覆盖

运行方式：

```powershell
.\build\mini_nn.exe agent
```

调试模式：

```powershell
.\build\mini_nn.exe agent --debug
```

启动网页对话界面：

```powershell
.\build\mini_nn.exe web-chat --model qwen3:8b --port 8080
```

启动后在浏览器打开：

```text
http://127.0.0.1:8080/
```

网页界面现在除了对话和审批，还会显示：

- 最近一次执行的步骤 trace
- 当前能力快照
- 最近一次自检时间和整体健康状态

如果你想直接打接口验证状态，可以访问：

- `GET /api/info`
- `GET /api/state`
- `GET /api/health`
- `GET /api/capabilities`
- `POST /api/self-check`

关闭调试模式：

```powershell
.\build\mini_nn.exe agent --no-debug
```

显式指定模型：

```powershell
.\build\mini_nn.exe agent --model qwen3:8b Read src/main.cpp and summarize print_usage.
```

单轮调用：

```powershell
.\build\mini_nn.exe agent qwen3:8b Use the calculator tool to compute (2 + 3) * 4.
```

带调试日志的单轮调用：

```powershell
.\build\mini_nn.exe agent --debug qwen3:8b Search the workspace for ToolRegistry.
```

让 Agent 写工作区文件：

```text
Create the file notes/hello.txt in the workspace with exactly the content "hello from the agent".
```

执行前会看到审批摘要和内容预览，输入 `y` 才会真正写入。

让 Agent 局部修改已有文件：

```text
Edit the file notes/todo.txt by replacing exactly the text "TODO" with "DONE".
```

如果模型能定位到唯一匹配位置，会先显示每条 patch 的前后预览，再执行替换。

如果你更想按行改整段，也可以让它这样做：

```text
Edit src/main.cpp by replacing lines 20-24 with the new block I describe.
```

让 Agent 执行白名单命令：

```text
Run the command ollama list and summarize the installed models.
```

执行前同样会先显示命令摘要和工作目录，再经过审批。

让 Agent 查看当前进程：

```text
List the running processes and tell me whether explorer.exe appears.
```

让 Agent 查看当前窗口：

```text
List the visible windows and summarize the top few titles.
```

让 Agent 打开应用：

```text
Open the application notepad.exe.
```

让 Agent 聚焦窗口：

```text
Focus the window titled "README - Notepad".
```

让 Agent 等待窗口出现：

```text
Wait for the Notepad window to appear before continuing.
```

让 Agent 查看当前窗口控件：

```text
Inspect the current window UI and list the visible controls.
```

让 Agent 点击某个控件：

```text
Click the UI element whose visible text contains "Save".
```

让 Agent 向当前窗口输入文本：

```text
Type the text "hello from the agent" into the focused window.
```

当前桌面执行链更适合这样组合：

```text
Open Notepad, wait for the window, focus it, and type "hello from the agent".
```

### 3. 配置覆盖

当前支持工作区根目录配置文件 `mini_nn_cpp.conf`，并允许环境变量覆盖。可配置项包括：

- `default_model`
- `ollama.host`
- `ollama.port`
- `ollama.path`
- `ollama.resolve_timeout_ms`
- `ollama.connect_timeout_ms`
- `ollama.send_timeout_ms`
- `ollama.receive_timeout_ms`
- `commands.allowed_executables`
- `commands.timeout_ms`
- `commands.max_output_bytes`
- `commands.allowed_subcommands.<executable>`
- `policy.read_only_tools`
- `policy.bounded_write_tools`
- `policy.bounded_command_tools`
- `policy.bounded_desktop_tools`
- `policy.block_high_risk`
- `agent.default_debug`
- `agent.max_model_steps`
- `agent.history_window`
- `agent.history_byte_budget`
- `approval.mode`

示例：

```text
default_model=qwen3:8b
ollama.host=127.0.0.1
ollama.port=11434
ollama.path=/api/generate
commands.allowed_executables=git,g++,cmake,ctest,ollama
commands.timeout_ms=15000
commands.max_output_bytes=4000
commands.allowed_subcommands.git=status,diff,log,branch,rev-parse,show
commands.allowed_subcommands.ollama=list,ps,show
policy.read_only_tools=calculator,list_dir,read_file,search_code
policy.bounded_write_tools=write_file,edit_file
policy.bounded_command_tools=run_command
policy.bounded_desktop_tools=open_app,focus_window
policy.block_high_risk=true
agent.default_debug=false
agent.max_model_steps=5
agent.history_window=12
agent.history_byte_budget=16000
approval.mode=prompt
```

`approval.mode` 当前支持：

- `prompt`
- `auto-approve`
- `auto-deny`

对应的环境变量覆盖包括：

- `MINI_NN_MODEL`
- `MINI_NN_OLLAMA_HOST`
- `MINI_NN_OLLAMA_PORT`
- `MINI_NN_OLLAMA_PATH`
- `MINI_NN_OLLAMA_RESOLVE_TIMEOUT_MS`
- `MINI_NN_OLLAMA_CONNECT_TIMEOUT_MS`
- `MINI_NN_OLLAMA_SEND_TIMEOUT_MS`
- `MINI_NN_OLLAMA_RECEIVE_TIMEOUT_MS`
- `MINI_NN_ALLOWED_EXECUTABLES`
- `MINI_NN_COMMAND_TIMEOUT_MS`
- `MINI_NN_COMMAND_MAX_OUTPUT_BYTES`
- `MINI_NN_ALLOWED_GIT_SUBCOMMANDS`
- `MINI_NN_ALLOWED_OLLAMA_SUBCOMMANDS`
- `MINI_NN_POLICY_READ_ONLY_TOOLS`
- `MINI_NN_POLICY_BOUNDED_WRITE_TOOLS`
- `MINI_NN_POLICY_BOUNDED_COMMAND_TOOLS`
- `MINI_NN_POLICY_BOUNDED_DESKTOP_TOOLS`
- `MINI_NN_POLICY_BLOCK_HIGH_RISK`
- `MINI_NN_AGENT_DEFAULT_DEBUG`
- `MINI_NN_AGENT_MAX_MODEL_STEPS`
- `MINI_NN_AGENT_HISTORY_WINDOW`
- `MINI_NN_AGENT_HISTORY_BYTE_BUDGET`
- `MINI_NN_APPROVAL_MODE`

## 设计取舍

这次重构刻意没有引入额外第三方 C++ 库，原因是先把项目结构和最小链路跑通更重要。当前版本：

- 不依赖 `libcurl` C++ 封装
- 不依赖 JSON 第三方库
- 直接复用本机已有的 `ollama`
- 已经拆出 `core/interfaces` 和 `platform/windows`，为后续 Linux/macOS 适配留出边界
- `main.cpp` 现在只依赖通用的 `PlatformAgentFactory`，不再直接耦合 Windows 组装细节

代价也很明确：

- `WindowsOllamaModelClient` 已经是原生 HTTP 调用，但 JSON 解析仍是最小实现，不是完整 JSON 库
- `ActionParser` 目前只支持“单个 JSON 对象 + 字符串字段”的最小协议
- `SearchCodeTool` 目前是简单字符串匹配，不支持正则或语义搜索
- `WriteFileTool` 目前是整文件覆盖，不支持局部 patch
- `EditFileTool` 目前只支持“单文件 + 多次精确匹配替换”或“单文件 + 多个行区间 patch”，不支持多文件修改或结构化语法级 diff
- `RunCommandTool` 目前虽然已经支持配置白名单，但依然不支持任意 shell
- `OpenAppTool` 当前允许直接启动本地应用，但还没有单独的应用白名单
- `FocusWindowTool` 当前依赖 Windows 前台窗口规则，个别窗口可能会拒绝被置前
- `RunCommandTool` 现在会按 `commands.max_output_bytes` 截断超大输出，避免一次命令结果把后续 prompt 撑爆
- `RunCommandTool` 现在会把 `stdout` / `stderr` 分开返回，并按 `commands.timeout_ms` 强制超时
- `WindowsAgentFactory` 会默认接入文件审计日志器，因此命令执行链路默认会写审计日志
- `WriteFileTool` 和 `EditFileTool` 现在也会把执行结果写入审计日志，和 `run_command` 共用同一条审计链路
- `write_file`、`edit_file` 和 `run_command` 默认仍然需要终端审批；虽然策略分组已经可配置，但不建议把默认边界放宽
- `Agent` 会同时按 `agent.history_window` 和 `agent.history_byte_budget` 修剪内存中的历史消息，避免长期运行时无限增长
- `AgentFactory` 现在会先通过 `ToolSetFactory` 组装默认工具，再把工具集合注入 `Agent`
- 当前只有 Windows 适配层，尚未实现 Linux / macOS / Android / iOS 适配器
- `--debug` 目前打印到标准错误，适合排查 Agent 回路，不适合当作稳定机器接口

这些都适合作为下一轮重构目标。

## 下一步建议

如果继续往 Agent 方向开发，优先级建议是：

1. 给模型通信、动作解析和策略层继续补测试，避免后续改动回退
2. 增加 Linux / macOS 适配器，实现同一套 `IModelClient / ICommandRunner / IFileSystem`
3. 给 `ActionParser` 增加更严格的 schema 校验和错误恢复
4. 继续增强 `EditFileTool`，支持更稳定的 diff / patch 语义和回滚
5. 增加一个更接近真实 CLI 的集成测试入口，覆盖配置加载和审批模式切换

## 构建

如果你的终端能直接用 `cmake`：

```powershell
cmake -S . -B build
cmake --build build
```

如果当前环境只有 `g++`，也可以直接编译：

```powershell
g++ -std=c++17 ^
  src/app/app_config.cpp ^
  src/app/agent_cli_options.cpp ^
  src/app/agent_runner.cpp ^
  src/app/approval_provider_factory.cpp ^
  src/app/file_audit_logger.cpp ^
  src/app/program_cli.cpp ^
  src/app/static_approval_provider.cpp ^
  src/app/web_approval_provider.cpp ^
  src/app/web_chat_cli_options.cpp ^
  src/app/web_chat_server.cpp ^
  src/main.cpp ^
  src/app/terminal_approval_provider.cpp ^
  src/matrix.cpp ^
  src/platform/platform_agent_factory.cpp ^
  src/platform/windows/windows_agent_factory.cpp ^
  src/platform/windows/ollama_json_utils.cpp ^
  src/platform/windows/windows_command_runner.cpp ^
  src/platform/windows/windows_file_system.cpp ^
  src/platform/windows/windows_ollama_model_client.cpp ^
  src/platform/windows/windows_process_manager.cpp ^
  src/platform/windows/windows_window_controller.cpp ^
  src/demo/training_demo.cpp ^
  src/agent/agent.cpp ^
  src/agent/action_parser.cpp ^
  src/agent/calculator_tool.cpp ^
  src/agent/edit_file_tool.cpp ^
  src/agent/focus_window_tool.cpp ^
  src/agent/list_dir_tool.cpp ^
  src/agent/list_processes_tool.cpp ^
  src/agent/list_windows_tool.cpp ^
  src/agent/open_app_tool.cpp ^
  src/agent/permission_policy.cpp ^
  src/agent/read_file_tool.cpp ^
  src/agent/run_command_tool.cpp ^
  src/agent/search_code_tool.cpp ^
  src/agent/tool_set_factory.cpp ^
  src/agent/tool_registry.cpp ^
  src/agent/workspace_utils.cpp ^
  src/agent/write_file_tool.cpp ^
  -lwinhttp ^
  -lws2_32 ^
  -o build/mini_nn.exe
```

内核测试可执行文件也可以直接编译：

```powershell
g++ -std=c++17 ^
  tests/kernel_tests.cpp ^
  src/agent/action_parser.cpp ^
  src/agent/agent.cpp ^
  src/agent/calculator_tool.cpp ^
  src/agent/edit_file_tool.cpp ^
  src/agent/focus_window_tool.cpp ^
  src/agent/list_dir_tool.cpp ^
  src/agent/list_processes_tool.cpp ^
  src/agent/list_windows_tool.cpp ^
  src/agent/open_app_tool.cpp ^
  src/agent/permission_policy.cpp ^
  src/agent/read_file_tool.cpp ^
  src/agent/run_command_tool.cpp ^
  src/agent/search_code_tool.cpp ^
  src/agent/tool_set_factory.cpp ^
  src/agent/tool_registry.cpp ^
  src/agent/workspace_utils.cpp ^
  src/agent/write_file_tool.cpp ^
  src/app/app_config.cpp ^
  src/app/agent_cli_options.cpp ^
  src/app/agent_runner.cpp ^
  src/app/approval_provider_factory.cpp ^
  src/app/file_audit_logger.cpp ^
  src/app/program_cli.cpp ^
  src/app/static_approval_provider.cpp ^
  src/app/terminal_approval_provider.cpp ^
  src/app/web_chat_cli_options.cpp ^
  src/platform/platform_agent_factory.cpp ^
  src/platform/windows/windows_agent_factory.cpp ^
  src/platform/windows/ollama_json_utils.cpp ^
  src/platform/windows/windows_command_runner.cpp ^
  src/platform/windows/windows_file_system.cpp ^
  src/platform/windows/windows_ollama_model_client.cpp ^
  src/platform/windows/windows_process_manager.cpp ^
  src/platform/windows/windows_window_controller.cpp ^
  -lwinhttp ^
  -o build/kernel_tests.exe
```

Agent 集成测试可执行文件也可以直接编译：

```powershell
g++ -std=c++17 ^
  tests/agent_integration_tests.cpp ^
  src/agent/action_parser.cpp ^
  src/agent/agent.cpp ^
  src/agent/calculator_tool.cpp ^
  src/agent/edit_file_tool.cpp ^
  src/agent/focus_window_tool.cpp ^
  src/agent/list_dir_tool.cpp ^
  src/agent/list_processes_tool.cpp ^
  src/agent/list_windows_tool.cpp ^
  src/agent/open_app_tool.cpp ^
  src/agent/permission_policy.cpp ^
  src/agent/read_file_tool.cpp ^
  src/agent/run_command_tool.cpp ^
  src/agent/search_code_tool.cpp ^
  src/agent/tool_set_factory.cpp ^
  src/agent/tool_registry.cpp ^
  src/agent/workspace_utils.cpp ^
  src/agent/write_file_tool.cpp ^
  src/app/app_config.cpp ^
  src/app/agent_cli_options.cpp ^
  src/app/agent_runner.cpp ^
  src/app/approval_provider_factory.cpp ^
  src/app/file_audit_logger.cpp ^
  src/app/program_cli.cpp ^
  src/app/static_approval_provider.cpp ^
  src/app/terminal_approval_provider.cpp ^
  src/app/web_chat_cli_options.cpp ^
  src/platform/windows/ollama_json_utils.cpp ^
  -o build/agent_integration_tests.exe
```

运行测试：

```powershell
.\build\kernel_tests.exe
```

```powershell
.\build\agent_integration_tests.exe
```

这组测试目前覆盖：

- `ActionParser`
- `PermissionPolicy`
- `Ollama` 顶层 JSON 字段解析
- 工作区配置文件加载
- `PermissionPolicy` 的配置覆盖
- `EditFileTool` 的精确替换、行区间 patch 和歧义匹配失败路径
- `RunCommandTool` 的白名单放行与拦截逻辑
- `Agent` 主循环的读文件回路、审批拒绝回路和格式错误自恢复回路
- `Agent` 的历史窗口、最大步数和静态审批模式
- CLI 参数解析、默认值继承和 CLI 覆盖配置的优先级
- 顶层命令分发和 usage 文本生成
- Agent 单轮执行和交互循环
- Windows 平台依赖组装工厂

集成测试目前覆盖：

- `approval.mode=prompt` 的真实审批回路
- `approval.mode=auto-approve` 的无交互命令执行回路
- `approval.mode=auto-deny` 的无交互拒绝回路
