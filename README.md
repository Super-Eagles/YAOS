# YAOS 使用说明

## 1. 系统是什么

YAOS 是一个基于 C++ / Qt / QML 的桌面 AI 运行时与控制台。它不是单纯的聊天界面，而是把下面几类能力放在同一个系统里：

- 模型与提供方管理
- 桌面对话工作台
- 多渠道消息网关
- 工具权限与人工审批
- 运行时拓扑与多节点委托
- 本地 / 集群记忆平面
- 插件、技能、MCP 扩展接入
- 自动化编排与定时执行
- 任务、事件、资源索引与运行日志

如果只看一句话，可以把它理解为：

> 一个可视化、可扩展、可审批、可自动化的本地 AI 控制台。

---

## 2. 系统组成

| 组件 | 作用 | 典型入口 |
| --- | --- | --- |
| Desktop Studio | 图形化配置、聊天、诊断、自动化、审批 | `yaos` / `yaos gui` |
| Runtime Core | Agent 主运行时，处理对话、工具调用、任务记录 | GUI 内嵌或 CLI |
| Gateway | 启动频道、心跳、调度循环 | `yaos gateway` |
| Local Daemon | 本地 IPC sidecar，提供守护式运行 | `yaos daemon` |
| Runtime Service | 本地 HTTP 运行时服务 | `yaos runtime-service` |
| Memory Service | 本地 HTTP 记忆服务 | `yaos memory-service` |
| Control Service | 本地 HTTP 控制平面服务 | `yaos control-service` |

默认情况下：

- 无参数运行 `yaos` 会打开桌面工作台
- `startAll.cmd` 会启动 `bin\yaos.exe gateway`
- GUI 和 CLI 共用同一套配置与工作区数据

---

## 3. 运行环境与构建

从源码、`YAOS.pro`、`yaosd.pro` 和生成的 `Makefile` 看，当前工程使用：

- Qt 5.14.2
- MSVC 2017 64-bit 工具链
- C++17
- qmake 工程文件：`yaos_base.pro`（基础静态库）、`yaos_business.pro`（业务静态库）、`YAOS.pro`（主程序）、`yaosd.pro`（本地 daemon）
- 网络层：FastNet（OpenSSL 3），通过 `src/platform/network/` 适配层接入，禁止直接使用 `QTcpSocket` / `QNetworkAccessManager`

如果你已经有可执行文件，可以跳过构建，直接看“首次使用”。

### Windows 下典型构建方式

推荐统一使用根目录的 `build.bat`。脚本会按固定顺序构建：

```text
yaos_base -> yaos_business -> yaos -> yaosd
```

开发调试：

```bat
cd /d D:\GITHUB\YAOS
build.bat --debug-only
```

验证发布：

```bat
cd /d D:\GITHUB\YAOS
build.bat --release-only
```

全量清理后重建：

```bat
cd /d D:\GITHUB\YAOS
build.bat --clean
build.bat --release-only
```

注意：`--clean` 只清理中间产物（`build/`、`lib/`、`release/`、`debug/`、`Makefile*`），不会自动重新构建，需要再跑一次构建命令。

只改界面时，可以只构建 GUI 入口和依赖库：

```bat
build.bat --debug-only --skip-daemon
```

只验证 daemon 时，可以跳过 GUI：

```bat
build.bat --debug-only --skip-gui
```

运行集成 smoke test：

```bat
test.bat -Config all
```

运行 GUI 回归：

```bat
gui_regression.bat -Config debug -Case runtime-page
```

并行 CMake 构建入口也已提供，主要用于后续逐步从 qmake 迁移：

```bat
cmake_build.bat --debug
cmake_build.bat --debug --target "yaos_tests"
```

构建输出通常在：

```text
bin\yaos.exe
bin\debug\yaos.exe
bin\yaosd.exe
bin\debug\yaosd.exe
lib\yaos_base.lib
lib\yaos_business.lib
lib\debug\yaos_based.lib
lib\debug\yaos_businessd.lib
```

说明：

- `build.bat` 默认会先运行架构边界检查，防止 QtNetwork/QtGui 依赖回退到底层或业务层。
- 临时跳过边界检查可使用 `--no-check`，但不建议提交前使用。
- 完整构建 GUI 和 daemon 后，`build.bat` 默认会运行发布目录检查，确认 Debug/Release DLL、OpenSSL 3、FastNet 和 `dumpbin` 依赖没有混用或回退；临时跳过可使用 `--no-runtime-check`。
- `YAOS.pro` 只负责 Qt/QML 前端入口。
- `yaosd.pro` 只负责本地 daemon 入口。
- `yaos_base.pro` 和 `yaos_business.pro` 生成底层/业务静态库。
- Release 输出到 `bin\`，Debug 输出到 `bin\debug\`。
- `startAll.cmd` 只是一个简化启动脚本，当前实际执行的是 `bin\yaos.exe gateway`。

---

## 3.5 架构分层

YAOS 内部分为四层，每层有明确的边界约束：

```
QML / Desktop UI
    |
StudioBridge          UI 适配、toast、窗口、OAuth loopback
    |
IStudioBackend        业务能力接口，三种实现：
    |
    +-- RuntimeFacadeStudioBackend    embedded 模式（GUI 进程内）
    +-- RemoteStudioBackend           daemon / remote 模式（跨进程）
    |
Runtime / Providers / Gateway / Memory / Control / Automation
    |
Base / FastNet / Config / Storage / Protocol
```

**三种运行模式**（由 `runtime.mode` 配置）：

| 模式 | 说明 | 适用场景 |
| --- | --- | --- |
| `embedded` | Runtime 在 GUI 进程内运行 | 默认，单机使用 |
| `daemon` | GUI 连接本地 `yaosd.exe` sidecar | 需要 GUI 关闭后 runtime 继续运行 |
| `remote` | GUI 连接远端 HTTP runtime service | 多机或服务器部署 |

daemon/remote 模式下，GUI 与 runtime 之间通过 `studio.*` invoke 协议通信，所有业务操作（provider 同步、OAuth、config 保存、chat 等）都在 runtime 端执行，GUI 只消费 DTO。

**模块边界约束**（由 `scripts/check_architecture.ps1` 强制检查）：

- `yaos_base` / `yaos_business`：`QT -= gui`，禁止 QtNetwork 直接调用
- 所有 HTTP/WebSocket/TLS 通过 `FastNetHttpTransport` / `FastNetWebSocketTransport`
- `StudioBridge` 不直接使用 runtime record，不直接创建 provider client，不直接发 control plane HTTP

---

## 4. 首次使用

建议按下面顺序启动系统。

### 第 1 步：初始化配置和工作区

```bat
yaos init
```

这个命令会做三件事：

- 生成默认配置文件
- 创建工作区目录
- 同步工作区模板与系统存储

### 第 2 步：打开桌面控制台

```bat
yaos
```

或者：

```bat
yaos gui
```

### 第 3 步：先配置模型

进入 `模型设置 Model Settings` 页面，至少完成下面两件事：

1. 配置一个可用的 provider
2. 选择默认模型

### 第 4 步：配置运行时

进入 `运行时控制台 Runtime` 页面，优先确认：

- 工作区目录
- Runtime 模式
- 记忆模式
- 工具能力
- Web Search 配置

### 第 5 步：配置安全策略

进入 `安全与审批中心 Security` 页面，确认：

- 哪些工具直接放行
- 哪些工具需要审批
- 是否保留审计与通知

### 第 6 步：如果需要频道接入，再启动网关

如果你要接 Telegram / Slack / Feishu / 邮件等外部入口：

1. 先在 `频道投递面板 Channels` 里填凭据
2. 再启动：

```bat
yaos gateway
```

---

## 5. 默认路径、端口与关键文件

### 默认路径

| 项目 | 默认值 |
| --- | --- |
| 配置文件 | `~/.yaos/config.json` |
| 工作区 | `~/.yaos/workspace` |

在 Windows 下，`~` 通常会展开到：

```text
%USERPROFILE%
```

也就是说常见实际路径是：

```text
C:\Users\<用户名>\.yaos\config.json
C:\Users\<用户名>\.yaos\workspace
```

### 默认端口

| 组件 | 默认地址 |
| --- | --- |
| Gateway | `0.0.0.0:18790` |
| Runtime Service | `http://127.0.0.1:18890` |
| Memory Service | `http://127.0.0.1:18891` |
| Control Service | `http://127.0.0.1:18892` |
| Heartbeat 间隔 | `1800` 秒 |

### 配置文件里的主要区块

`config.json` 的根结构重点包括：

- `agents.defaults`
- `providers`
- `extensions`
- `tools`
- `security`
- `channels`
- `gateway`
- `deployment`
- `runtime`
- `memory`

### 关键模式值

| 配置项 | 可选值 | 含义 |
| --- | --- | --- |
| `deployment.mode` | 常用值是 `standalone` / `cluster` | 单机或集群；源码也兼容 `local_cluster` / `sidecar` 这类旧值 |
| `runtime.mode` | `embedded` / `daemon` / `remote` | 内嵌、本地守护、HTTP 远端运行时 |
| `memory.mode` | `legacy` / `layered` | 传统摘要记忆、分层记忆 |
| `memory.backend` | `legacy` / `hybrid_local` / `hybrid_cluster` | 传统、本地混合、集群混合后端 |

---

## 6. 桌面控制台怎么用

YAOS 的 GUI 是主操作入口，页面已经按职责拆好。

### 6.1 总览中枢 Overview

这里是启动后最先看的页，适合用来观察系统当前状态。

主要看点：

- 最近任务 Recent Tasks
- 系统事件 System Events
- 任务树聚合 Task Trees
- 运行快照 Runtime Snapshot
- 资源索引统计 Resource Index

适合做的事情：

- 快速判断运行时是否正常
- 看最近一次任务成功还是失败
- 看有没有审批、通知、自动化、资源积压

### 6.2 模型设置 Model Settings

这是模型和提供方的总入口。

这里可以做的事情：

- 录入 API Key / API Base
- 处理 OAuth 登录状态
- 拉取 provider 的实时模型目录
- 指定默认 provider 和默认 model

源码里当前注册的 provider 规范 ID 包括：

- `custom`
- `azure_openai`
- `openrouter`
- `aihubmix`
- `siliconflow`
- `volcengine`
- `anthropic`
- `openai`
- `codebuddy`
- `openai_codex`
- `github_copilot`
- `deepseek`
- `gemini`
- `zhipu`
- `dashscope`
- `moonshot`
- `minimax`
- `vllm`
- `groq`

补充说明：

- GUI 内部有些配置槽位仍使用 camelCase 命名，但 CLI、provider registry 和规范 ID 以 snake_case 为准

建议：

- 先保证至少一个 provider 可用
- 默认 provider 可以保留 `auto`，但默认 model 最好明确指定

### 6.3 对话工作台 Chat

这是日常使用频率最高的页面。

功能包括：

- 输入任务 / 指令
- 按次覆盖 provider 或 model
- 查看 Markdown 渲染结果
- 查看执行轨迹
- 一键复制输出

适合的用法：

- 日常桌面对话
- 对当前工作区发起分析、生成、修复、审查任务
- 对单次消息临时切换模型

### 6.4 运行时控制台 Runtime

这是系统最核心也最复杂的页面，负责运行拓扑和高级能力。

它大致分成几块：

#### Agent Core

配置默认工作区、默认模型参数、最大 token、温度、工具迭代次数等。

#### Gateway

配置网关入口和心跳：

- host
- port
- heartbeat interval

#### Runtime Topology

控制运行形态：

- 单机还是集群
- 内嵌还是守护进程
- 远端运行时是否启用
- 本地 daemon / 本地 HTTP runtime 是否自动拉起

#### Memory Plane

配置记忆：

- 传统记忆还是分层记忆
- 本地还是集群后端
- 是否启用远端记忆服务
- 记忆服务 endpoint 与 timeout

当前这张卡里几个关键字段的真实含义是：

- `memory.mode = legacy`：沿用传统摘要记忆，Agent 侧主要读写 `memory/MEMORY.md` 和 `memory/HISTORY.md`
- `memory.mode = layered`：启用分层记忆，当前实现会接入会话库、事实库、回忆检索、每日导出与兼容 Markdown 导出
- `memory.backend = hybrid_local`：优先使用本地 SQLite / 文件型后端
- `memory.backend = hybrid_cluster`：优先使用远端 `memory-service`；如果远端不可达，当前实现会回退到本地存储
- `启用每日摘要`：生成 `memory/daily/*.summary.md`，并在启用 Markdown 导出时回写 `memory/MEMORY.md` / `memory/HISTORY.md`
- `检索 TopK`：控制每轮发给模型前，最多注入多少条 recall 结果
- `近期窗口`：界面文案写的是“小时”，但当前实现实际按“排除最近 N 条消息，不参与情景回忆检索”处理，不是真正的时间窗口

关于远端记忆服务，还要注意一个容易误解的点：

- Runtime 页状态刷新只会探测 `memory-service` 是否可达，不会因为勾了“自动拉起本地记忆服务”就直接起进程
- 只有程序真正走到远端记忆后端那条代码路径时，才会在本机 endpoint 上尝试 auto-spawn
- 所以如果你只是想验证本地拆分服务最稳妥，仍然建议先手工执行一次 `yaos memory-service`

#### Node Directory

查看节点目录、在线状态、负载、并发能力。

#### Routing Diagnostics

做委托路由预演，帮助判断某个任务会被派到哪个节点。

#### Delegation Templates / Draft / Batch Draft

用来管理常用委托模板、单任务委托草稿、批量委托草稿。

#### Tool Capabilities

决定 Agent 能注册哪些工具能力：

- web
- filesystem
- exec
- messaging
- spawn
- cron
- mcp

#### Web Search

配置网页搜索链路，包括：

- 搜索提供方
- API key
- 最大结果数
- 代理

### 6.5 安全与审批中心 Security

这个页面决定系统“能不能直接做”。

主要模块：

- Tool Policies
- Security Telemetry
- Approvals
- Notifications

默认工具策略里，重要的几个行为是：

- `readFile`：`allow`
- `listDir`：`allow`
- `message`：`allow`
- `writeFile`：`confirm`
- `spawn`：`confirm`
- `cron`：`confirm`
- `mcpCall`：`confirm`
- `pluginCall`：`confirm`
- `exec`：`deny`

也就是说，默认更偏安全：

- 读文件可以直接做
- 写文件通常要确认
- 执行系统命令默认禁止
- 多数敏感扩展动作需要审批

### 6.6 频道投递面板 Channels

这里负责外部消息入口和投递策略。

源码里支持的频道包括：

- Telegram
- Slack
- WhatsApp
- Feishu
- DingTalk
- Discord
- Matrix
- Email
- Mochat
- QQ

其中 Telegram / Slack / WhatsApp / Feishu / DingTalk / Discord 在 GUI 频道页有配置卡片，可以直接填写凭据。Matrix / Email / Mochat / QQ 目前只有后端实现，需要直接编辑 `config.json` 的 `channels` 区块来配置。

典型使用方式：

1. 在 Channels 页填好 token / host / allowFrom 等参数
2. 保存后启动 `yaos gateway`
3. 观察是否开始收发消息

### 6.7 扩展能力装配区 Extensions

这个页面负责三种扩展：

- Plugin
- Skill
- MCP

可以做的事情：

- 从内置目录安装插件 / 技能 / MCP 模板
- 为每个插件单独指定 provider 与 model
- 为技能设置启用状态和触发词
- 直接编辑 `tools.mcpServers`

扩展发现规则：

- 工作区插件目录：`<workspace>/plugins`
- 工作区技能目录：`<workspace>/skills`
- 应用目录内也支持 bundled 扩展：`yaos-plugins` / `yaos-skills`

### 6.8 自动化编排中枢 Automation

这是把“常用提示词”变成“可调度执行单元”的地方。

功能包括：

- 自动化列表
- 自动化编辑器
- 运行历史
- 下次运行时间
- 最近结果预览

适合做的事情：

- 每天汇总日志
- 定时巡检工作区
- 周期性生成日报 / 周报 / 摘要
- 固化高频分析任务

### 6.9 资源索引大厅 Resources

这是系统自动汇总出来的资源目录。

会索引的内容包括：

- 工作区顶层文档：`AGENTS.md`、`SOUL.md`、`USER.md`、`TOOLS.md`、`HEARTBEAT.md`
- `memory/*.md`
- `sessions/*.jsonl`
- 任务、事件、审批、通知、自动化
- 已发现的插件和技能

如果你想知道“系统现在到底持有哪些对象”，看这里最直观。

---

## 7. 常用 CLI 命令

### 7.1 基础命令

```bat
yaos help
yaos init
yaos status
yaos
yaos gui
yaos dashboard
yaos config
```

说明：

- `yaos`：打开桌面工作台
- `yaos gui`：打开桌面工作台
- `yaos dashboard`：直接进入总览页
- `yaos init`：初始化配置与工作区
- `yaos status`：输出运行时状态摘要
- `yaos config`：直接打开图形化配置页，默认落在模型 / provider 配置区域

### 7.2 Agent 直接调用

```bat
yaos agent
yaos agent --message "帮我总结当前工作区"
yaos agent -m "检查最近失败任务"
yaos agent --interactive
yaos agent --provider openai --model gpt-4.1 --message "只跑一次"
```

行为说明：

- 不带 `--message` 时，在桌面版里会直接打开 Chat 页面
- 带 `--message` 或 `-m` 时，会做一次性 CLI 调用
- 带 `--interactive` 时，强制进入命令行交互模式，不再跳转桌面页面
- 可以对单次运行覆盖 provider 和 model

### 7.3 Provider 相关命令

```bat
yaos provider-login <provider> [--wait] [--sync-models] [--timeout <seconds>] [--json]
yaos provider-poll <provider> [--wait] [--sync-models] [--timeout <seconds>] [--json]
yaos provider-refresh <provider> [--json]
yaos provider-logout <provider> [--json]
yaos provider-status <provider> [--refresh] [--json]
yaos provider-models <provider> [--json] [--no-refresh]
```

常见用法示例：

```bat
yaos provider-status openai
yaos provider-models openai --json
yaos provider-login github_copilot --wait --sync-models
```

### 7.4 多节点委托 / 模板命令

```bat
yaos route-preview [--template <id>] [--role <role>] [--tags <a,b>] [--tool <tool>]
yaos submit-delegation [--template <id>] [--task <text>] [--label <text>] [--node <nodeId>] [--role <role>]
yaos template-export [--id <templateId>] [--output <path.json>] [--json]
yaos template-import --file <path.json> [--replace] [--json]
yaos template-push [--id <templateId>] [--endpoint <url>] [--replace] [--json]
yaos template-pull [--endpoint <url>] [--replace] [--json]
```

示例：

```bat
yaos route-preview --role desktop --tags review,gpu --tool spawn
yaos submit-delegation --task "扫描当前工作区风险" --role desktop --label "安全扫描"
yaos template-export --output delegation-templates.json
yaos template-import --file delegation-templates.json --replace
```

说明：

- `route-preview` 用于看路由会落到哪个节点
- `submit-delegation` 用于真正提交委托
- `template-push` / `template-pull` 依赖 `deployment.controlPlaneUrl` 或手动传 `--endpoint`

### 7.5 服务与网关命令

```bat
yaos gateway
yaos daemon [--server <name>]
yaos runtime-service [--endpoint <http://host:port>] [--advertise-endpoint <http://host:port>]
yaos memory-service [--endpoint <http://host:port>] [--api-key <token>]
yaos control-service [--endpoint <http://host:port>]
```

示例：

```bat
yaos gateway
yaos daemon
yaos runtime-service --endpoint http://127.0.0.1:18890
yaos memory-service --endpoint http://127.0.0.1:18891
yaos control-service --endpoint http://127.0.0.1:18892
```

补充说明：

- 这里只列了最常用参数；`route-preview`、`submit-delegation`、`agent`、`daemon`、`runtime-service`、`memory-service`、`control-service` 都还有额外选项，建议配合 `yaos <command> --help` 查看完整用法

### 7.6 GUI 回归与诊断启动

这个模式不是普通 CLI 子命令分发出来的，而是由 `src/main.cpp` 直接判定启动。

```bat
yaos gui-regression --case security
yaos gui-regression --case runtime --json-out out.json
```

常见参数包括：

- `--case`
- `--json-out`
- `--switch-count`
- `--timeout-ms`
- `--page-settle-ms`
- `--heartbeat-interval-ms`
- `--max-ui-gap-ms`

---

## 8. 典型使用场景

### 场景 1：本机桌面单机使用

适合：

- 自己在桌面上和 Agent 对话
- 不需要外部频道
- 不需要远端服务

推荐配置：

- `deployment.mode = standalone`
- `runtime.mode = embedded`
- `memory.mode = legacy` 或 `layered`
- 不启动 `gateway`

### 场景 2：接入外部频道做消息入口

适合：

- Telegram / Slack / 飞书 / 邮件等消息接入

推荐步骤：

1. 在 `Channels` 页配置渠道凭据
2. 在 `Security` 页确认审批策略
3. 启动 `yaos gateway`
4. 用 `Overview` 和 `Channels` 页观察收发状态

### 场景 3：本地拆分 Runtime / Memory 服务

适合：

- 想把 GUI 和运行时服务解耦
- 想通过 HTTP 服务形式调试 runtime / memory

推荐配置：

- `runtime.mode = remote`
- `runtime.endpoint = http://127.0.0.1:18890`
- `memory.service.enabled = true`
- `memory.service.endpoint = http://127.0.0.1:18891`
- `memory.mode = layered`
- `memory.backend = hybrid_cluster`

可选做法：

- 开启自动拉起本地 runtime service
- 开启自动拉起本地 memory service

补充说明：

- Runtime 页的状态探测本身不会自动拉起 `memory-service`
- 如果你要验证 memory HTTP 服务链路，最直接的做法仍然是先手工启动 `yaos memory-service`

### 场景 4：做集群 / 控制平面实验

适合：

- 多节点协同
- 远端控制平面
- 跨节点委托和路由诊断

重点配置：

- `deployment.mode = cluster`
- `deployment.clusterId`
- `deployment.nodeId`
- `deployment.nodeRole`
- `deployment.nodeTags`
- `deployment.controlPlaneUrl`
- `deployment.registryUrl`

常用页面：

- `Node Directory`
- `Routing Diagnostics`
- `Delegation Templates`
- `Delegation Draft`

### 场景 5：做自动化调度

适合：

- 每日摘要
- 周期巡检
- 定时汇总

操作建议：

1. 先在 Chat 页把提示词试好
2. 再进入 `Automation` 页创建自动化
3. 指定 provider、model、prompt、调度方式
4. 保存后看 `Run History`

---

## 9. 工作区里会生成哪些数据

YAOS 的很多状态都持久化在工作区，而不是只存在内存里。

常见目录 / 文件如下：

| 路径 | 作用 |
| --- | --- |
| `runtime/tasks.json` | 任务记录 |
| `runtime/approvals.json` | 审批记录 |
| `runtime/notifications.json` | 通知记录 |
| `runtime/events.jsonl` | 事件日志 |
| `runtime/logs/*.jsonl` | 结构化运行日志 |
| `runtime/control_node_health.json` | control plane 节点健康快照 |
| `automations/flows.json` | 自动化定义 |
| `automations/runs.json` | 自动化运行记录 |
| `cron/jobs.json` | 调度任务状态 |
| `sessions/*.jsonl` | 会话记录 |
| `memory/MEMORY.md` | 分层记忆导出的长期记忆汇总 |
| `memory/HISTORY.md` | 分层记忆导出的历史汇总 |
| `memory/daily/*.md` | 分层记忆的每日导出 |
| `memory/daily/*.summary.md` | 每日摘要导出 |
| `plugins/` | 工作区插件 |
| `skills/` | 工作区技能 |

其中结构化日志会写到：

```text
<workspace>/runtime/logs/<service>-<pid>.jsonl
```

这些文件的意义：

- GUI 页面很多数据都直接从这里读取
- 你可以备份工作区来迁移运行状态
- 排错时优先看 `runtime/events.jsonl`、`runtime/logs/` 和 `runtime/control_node_health.json`

---

## 10. 安全机制怎么理解

YAOS 的设计不是“什么都直接执行”，而是“按策略执行”。

系统会基于工具策略决定：

- 直接允许
- 要求人工审批
- 直接拒绝

审批相关页面在 `Security` 页，落盘数据在：

```text
<workspace>/runtime/approvals.json
<workspace>/runtime/notifications.json
```

如果你发现某个动作“没执行”，优先检查：

1. 是不是被 policy 拦住了
2. 是不是进入了审批队列
3. 是不是通知中心有未处理消息

---

## 11. 常见排错方法

### 11.1 先看整体状态

```bat
yaos status
```

这个命令会告诉你：

- 配置文件是否存在
- 工作区是否就绪
- 默认模型与实际 provider
- runtime / control plane / memory service 是否可达
- 频道数量
- 任务、事件、审批、通知、自动化数量

### 11.2 模型不可用

按下面顺序检查：

1. `Models` 页里 provider 是否真的填了 key 或完成了 OAuth
2. 执行 `yaos provider-status <provider>`
3. 必要时执行 `yaos provider-models <provider>`
4. 看默认 model 是否在该 provider 的可用模型列表里

### 11.3 外部频道没有响应

排查顺序：

1. `Channels` 页里该渠道是否启用
2. 凭据是否完整
3. `allowFrom` 是否把发送方挡住了
4. `yaos gateway` 是否真的在运行
5. `Overview` / `System Events` 是否有错误日志

### 11.4 Runtime / Memory 服务不可达

排查顺序：

1. 地址是否正确，默认是 `18890` / `18891` / `18892`
2. `runtime.mode` 是否真的切到了 `remote`
3. `memory.mode` / `memory.backend` 是否真的走到了远端记忆链路
4. 是否允许自动拉起本地服务
5. 注意 Runtime 页状态探测本身不会自动拉起 `memory-service`
6. 先手工执行 `yaos runtime-service` / `yaos memory-service` 验证端口和进程是否正常
7. 端口是否被占用
8. 查看 `runtime/logs/` 下对应服务日志

### 11.5 自动化没有按时执行

排查顺序：

1. 自动化是否启用
2. 调度方式是否保存成功
3. `Run History` 里有没有失败记录
4. `cron/jobs.json` 是否存在并更新
5. 默认 provider / model 是否可用

### 11.6 明明发出了任务，但工具没跑

优先检查：

1. `Security` 页的 `Tool Policies`
2. `Approvals`
3. `Notifications`
4. 工具能力是否在 `Runtime -> Tool Capabilities` 中启用

---

## 12. 建议的实际使用顺序

如果你要把 YAOS 真正用起来，而不是只打开看看，建议按这个顺序：

1. `yaos init`
2. 打开 GUI
3. 在 `Models` 页配置 provider 和 model
4. 在 `Runtime` 页确认工作区、runtime、memory、tool capabilities
5. 在 `Security` 页确认审批策略
6. 在 `Chat` 页做一次真实对话
7. 需要外部渠道时再配 `Channels` 并启动 `yaos gateway`
8. 高频任务稳定后再迁到 `Automation`
9. 需要多节点时再启用 `Routing Diagnostics` 和委托模板

这样问题最少，也最容易定位故障。
