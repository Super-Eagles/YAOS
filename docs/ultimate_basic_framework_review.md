# UltimateBasicFramework 复用评估

评估路径：`D:\projectC++\DEMO\UltimateBasicFramework`

## 顶级模块

该项目按独立库组织，主要模块包括：

- `application`：应用生命周期、全局单例、启动流程
- `config`：TOML 配置管理
- `log`：spdlog 异步日志
- `threadpool`：Taskflow 线程池
- `timer`：基于 eventloop 的定时器
- `metrics`：指标采集
- `trace`：跟踪辅助
- `network`：TCP/HTTP/WebSocket 网络库
- `database`：SQLite/MySQL/Redis 访问
- `rpc`：RPC 框架
- `crypto`：OpenSSL 加密封装
- `json`：RapidJSON/simdjson
- `serialization`：FlatBuffers
- `compression`、`excel`、`memorypool`、`objectpool`、`registry`、`gateway` 等

## 与 YAOS 当前能力对比

| 能力 | YAOS 当前状态 | UBF 模块 | 结论 |
| --- | --- | --- | --- |
| 网络 | 已统一到 FastNet | `network` | 不复制，避免绕开 FastNet |
| 配置 | `src/config` 已有 JSON/结构化配置 | `config` | 不复制，避免 TOML 体系并存 |
| 日志 | `StructuredLog` 已接入运行时 | `log` | 暂不复制，只保留设计参考 |
| 线程池 | QtConcurrent/FastNet 线程模型已在用 | `threadpool` | 暂不复制，后续有 CPU 密集任务再评估 |
| 定时器 | Qt timer/runtime cron 已有 | `timer` | 不复制 |
| 指标 | YAOS 目前缺轻量指标汇总 | `metrics` | 只作为后续参考，不直接复制 |
| trace | YAOS 缺统一 trace id 工具 | `trace` | 可参考，但先不引入外部依赖 |
| database | YAOS 已有 SQLite store | `database` | 不复制 |
| rpc | YAOS 目前以 daemon/FastNet/HTTP 协议为主 | `rpc` | 不复制 |
| crypto | OpenSSL 已由 FastNet 使用 | `crypto` | 不复制 |

## 处理决定

本轮不直接复制 UBF 顶级模块。理由：

1. 大多数模块与 YAOS 已有能力重叠。
2. UBF 依赖链较重，包括 Boost、spdlog、Taskflow、OpenSSL、SQLite、hiredis、simdjson、FlatBuffers 等。
3. 直接复制会增加二套网络、二套配置、二套日志，破坏当前 `base/business/frontend/daemon` 边界。
4. 当前最重要的架构要求是网络调用统一走 FastNet，UBF 的 `network` 模块不应引入。

## 可借鉴项

后续如果需要，可以只借鉴设计，不复制代码：

- `threadpool`：用于将来 CPU 密集型 agent tool 的任务调度设计参考。
- `metrics`：用于将来 runtime 页面的指标聚合模型参考。
- `trace`：用于将来跨 daemon、HTTP service、agent task 的 trace id 传播参考。
- 根级 CMake 组织方式：用于 YAOS 并行 CMake 迁移参考。

## 复用约束

如后续确实要复制 UBF 模块，必须满足：

- 不引入 QtNetwork。
- 不替代 FastNet 网络路径。
- 不让 `yaos_base` 或 `yaos_business` 依赖 QtGui。
- 不复制与 YAOS 已有 config/log/database/runtime 重复的实现。
- 复制后必须纳入 `scripts/check_architecture.ps1` 边界检查。
