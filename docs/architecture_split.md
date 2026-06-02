# YAOS 模块拆分记录

本轮已经把 qmake 层面的源码清单拆分升级为两个 C++ 静态库和两个进程入口：

- `yaos_base.pro` 生成 `yaos_base.lib` / `yaos_based.lib`
- `yaos_business.pro` 生成 `yaos_business.lib` / `yaos_businessd.lib`
- `YAOS.pro` 只编译 Qt/QML 前端入口并链接业务库、底层库
- `yaosd.pro` 只编译 daemon 入口并链接业务库、底层库

## 模块边界

### 底层处理 `qmake/modules/base.pri`

底层库负责不依赖 Qt Quick/UI 的基础设施：

- 配置加载和基础配置类型
- FastNet HTTP/WebSocket 适配层
- 本地 daemon loopback 协议与服务端
- 分布式节点、任务总线、P2P、远程控制客户端
- 消息总线与会话管理

构建约束：

- `yaos_base.pro` 使用 `QT += core concurrent sql`
- `yaos_base.pro` 显式 `QT -= gui`
- 源码不得 include `src/ui/*`、provider/channel/agent 业务模块

### 业务层处理 `qmake/modules/business.pri`

业务库负责运行时和产品业务能力：

- runtime、任务、插件、技能、MCP、自动化、心跳
- provider、channel、agent tool、上下文构建
- memory 后端、远程 memory client、SQLite store
- GUI 应用可复用的业务入口，如 `ApplicationController`、`RuntimeHttpServer`、`MemoryHttpServer`、`ControlHttpServer`

构建约束：

- `yaos_business.pro` 使用 `QT += core concurrent sql`
- `yaos_business.pro` 显式 `QT -= gui`
- 业务层网络调用必须走 FastNet 适配，不再使用 QtNetwork
- `WebTools.cpp` 已去掉 `QTextDocument`，避免业务库拉入 QtGui
- 应用业务入口清单使用 `YAOS_BUSINESS_APP_*` 命名，避免被误解为 QtGui/UI 代码

### 前端处理 `qmake/modules/frontend.pri`

前端目标只放 Qt 前端入口和 UI 适配：

- `src/main.cpp`
- `src/platform/network/FastNetLifecycle.cpp`（GUI 进程专用的 FastNet 生命周期注册，仅前端需要）
- `src/ui/*`
- QML 资源仍由 `YAOS.pro` 引入

构建约束：

- `YAOS.pro` 链接 `yaos_business` 和 `yaos_base`
- Qt Quick/QML/Controls2 只保留在 GUI 入口

### daemon 入口 `qmake/modules/daemon.pri`

daemon 目标只放进程入口：

- `src/daemon/DaemonMain.cpp`

构建约束：

- `yaosd.pro` 链接 `yaos_business` 和 `yaos_base`
- `yaosd.pro` 显式 `QT -= gui`
- dumpbin 已确认 `yaosd.exe` 不直接依赖 QtGui/QtConcurrent

## 构建方式

可以直接使用根目录脚本：

```bat
build.bat
build.bat --debug-only
build.bat --release-only
build.bat --clean
build.bat --with-tests
test.bat -Config all
gui_regression.bat -Config debug -Case runtime-page
```

构建顺序固定为：

1. `yaos_base`
2. `yaos_business`
3. `yaos`
4. `yaosd`

输出目录：

- Release 运行目录：`bin`
- Debug 运行目录：`bin/debug`
- Release 静态库：`lib`
- Debug 静态库：`lib/debug`

## FastNetHttpTransport API 说明

`FastNetHttpTransport` 位于 `src/platform/network/FastNetHttpTransport.h`，是底层库（`yaos_base`）唯一合法的 HTTP 网络出口。

### 同步请求

```cpp
static HttpResponse send(const HttpRequest &request);
```

阻塞调用，返回完整响应。适用于一次性 REST 请求（模型列表、音频转录等）。

### 流式请求

```cpp
using StreamChunkCallback    = std::function<bool(const QByteArray &chunk)>;
using StreamCompleteCallback = std::function<void(int statusCode, const QString &error)>;

static bool sendStreaming(const HttpRequest &request,
                          StreamChunkCallback onChunk,
                          StreamCompleteCallback onDone);
```

非阻塞调用，适用于 SSE / 流式 LLM 响应。

- `onChunk`：每收到一个数据块时在 FastNet IO 线程上调用；返回 `false` 可提前中止流。
- `onDone`：流结束（正常或出错）时调用一次，携带 HTTP 状态码和错误描述。
- 返回值：`false` 表示连接无法建立（请求未发出）；`true` 表示连接已启动，后续结果通过回调通知。

**线程注意**：两个回调均在 FastNet IO 线程上触发，不在 Qt 主线程。如需更新 UI 或访问 Qt 对象，必须通过 `QMetaObject::invokeMethod` 或信号/槽跨线程派发。

## FastNet TLS 配置说明

`FastNetHttpTransport::send()` 在对 HTTPS URL 构建 `SSLConfig` 时，当前设置：

```cpp
sslCfg.enableSSL = true;
sslCfg.verifyPeer = false;          // 见下方说明
sslCfg.hostnameVerification = host; // SNI
```

`verifyPeer` 设为 `false` 的原因：OpenSSL 3 在 Windows 上不会自动读取系统证书存储（Windows Certificate Store），FastNet 也未随附 `cacert.pem`，导致 `verifyPeer = true` 时握手必然失败（`SSL_AD_HANDSHAKE_FAILURE`，错误码 `0A000410`）。

**安全影响**：禁用 peer 验证意味着不校验服务端证书链，存在中间人攻击风险。这是当前的临时方案，后续应通过以下任一方式恢复验证：

1. 将 `cacert.pem`（Mozilla 根证书包）打包到 `bin\` 并在 `SSLConfig` 中指定路径。
2. 调用 Windows CryptoAPI 导出系统根证书并注入 OpenSSL 信任库。

在此之前，所有 HTTPS 请求（LLM Provider API、模型列表、音频转录等）均在无证书验证的情况下运行。

## FastNet 线程池配置

`FastNetHttpTransport` 和 `FastNetWebSocketTransport` 共享同一个 `ensureFastNetInitialized()` 初始化函数（`std::once_flag` 保证只执行一次）。

线程数策略（`FastNetHttpTransport.cpp` 和 `FastNetWebSocketTransport.cpp` 保持一致）：

```cpp
const size_t hw = static_cast<size_t>(std::thread::hardware_concurrency());
const size_t threads = std::max<size_t>(4, std::min<size_t>(hw, 8));
FastNet::initialize(threads);
```

- 最小 4 线程：Thread[0] 是 IO poller，其余为任务 worker，保证并发 LLM 流式 + WebSocket channel + 本地 HTTP server 同时运行时不会全部排队在单个 worker 上。
- 最大 8 线程：避免在高核心数机器上过度占用系统资源。
- 实际线程数取 `[4, min(hardware_concurrency, 8)]`。

如需调整线程数上限，修改 `FastNetHttpTransport.cpp` 中 `ensureFastNetInitialized()` 的 `std::min` 参数即可，两个 Transport 文件需保持同步。

## FastNet 生命周期管理

`src/platform/network/FastNetLifecycle.h` 提供了一个轻量的生命周期辅助函数：

```cpp
namespace yaos::platform {
    // 在 QCoreApplication 创建后调用一次，将 FastNet::cleanup() 注册到 aboutToQuit 信号。
    void registerFastNetCleanup();
}
```

在 GUI 和 daemon 入口初始化时调用此函数，确保进程退出时 FastNet 资源被正确释放，避免 OpenSSL/TLS 上下文泄漏。

## 当前验证点

- Debug/Release 的 `yaos_base`、`yaos_business`、GUI、daemon 均可构建
- 源码层不再使用 `QSslSocket`、`QTcpSocket`、`QTcpServer`、`QUdpSocket`、`QLocalSocket`、`QNetworkAccessManager` 等 QtNetwork 网络类
- `YAOS.pro` 和 `yaosd.pro` 不再声明 `QT += network`
- FastNet/OpenSSL 3 DLL 由构建后步骤复制到运行目录
- Qt Debug 弹窗的 `QQuickStyle::setStyle("Basic")` 问题已改为 `Fusion`

## Provider 响应结构说明

### AnthropicProvider — Extended Thinking 支持

`AnthropicProvider` 在解析非流式响应时，会遍历 `content` 数组中的所有 block：

- `type == "text"`：文本内容，拼接到 `LLMResponse::content`。
- `type == "thinking"`：扩展思考内容（Anthropic Extended Thinking），拼接到 `LLMResponse::thinking`。
- 其他 block 类型（如 `tool_use`）：按原有逻辑处理。

`LLMResponse::thinking` 字段仅在 Anthropic 启用了 Extended Thinking 时才有内容，其他 provider 该字段为空字符串。上层调用方（如 `AgentLoop`、Chat 页面）在展示或记录时应判断该字段是否为空。

### AnthropicProvider — chatStreaming() 流式实现

`AnthropicProvider::chatStreaming()` 通过 `FastNetHttpTransport::sendStreaming()` 实现 SSE 流式对话，行为如下：

- 在请求 payload 中设置 `"stream": true`，触发 Anthropic SSE 响应。
- 内部维护一个 `lineBuffer`（`QByteArray`），跨 chunk 拼接不完整的 SSE 行，确保每行完整后再解析。
- 支持以下 SSE 事件类型：
  - `content_block_delta` / `text_delta`：增量文本，实时通过 `callback` 推送 `LLMStreamChunk::contentDelta`。
  - `content_block_delta` / `thinking_delta`：Extended Thinking 增量，推送 `LLMStreamChunk::thinkingDelta`。
  - `content_block_delta` / `input_json_delta`：工具调用参数增量，按 `index` 累积到 `toolCallArgBuffers`。
  - `content_block_start`（`tool_use` 类型）：记录工具调用 ID 和名称。
  - `message_delta`：提取 `stop_reason` 写入 `out.finishReason`。
- 流结束后，从 `toolCallArgBuffers` 重建完整的 `ToolCallRequest` 列表。
- 使用 `std::mutex` + `std::condition_variable` 阻塞调用线程，直到 `onDone` 回调触发。
- 流结束时向 `callback` 发送 `LLMStreamChunk::done = true` 信号。
- Extended Thinking 启用时，`temperature` 强制设为 `1.0`（Anthropic 要求）。

**线程注意**：`onChunk` 和 `onDone` 均在 FastNet IO 线程触发，`callback` 也在该线程调用。如需更新 UI，必须通过 `QMetaObject::invokeMethod` 或信号/槽跨线程派发。

### OpenAICompatibleProvider — chatStreaming() 流式实现

`OpenAICompatibleProvider::chatStreaming()` 通过 `FastNetHttpTransport::sendStreaming()` 实现 SSE 流式对话，与 `AnthropicProvider` 的流式实现并列，覆盖所有兼容 OpenAI Chat Completions API 的 provider（OpenAI、Azure OpenAI、DeepSeek、OpenRouter、Volcengine、DashScope 等）。

签名与 `LLMProvider` 接口一致：

```cpp
agent::LLMResponse chatStreaming(
    const QJsonArray &messages,
    const QJsonArray &tools,
    const QString &model,
    double temperature,
    int maxTokens,
    LLMStreamCallback callback
) override;
```

行为要点：

- 在请求 payload 中设置 `"stream": true`，触发 OpenAI 兼容的 SSE 响应。
- 解析 `choices[0].delta.content` 增量文本，实时通过 `callback` 推送 `LLMStreamChunk::contentDelta`。
- 解析 `choices[0].delta.tool_calls` 增量，按 `index` 累积工具调用参数，流结束后重建完整 `ToolCallRequest` 列表。
- 使用 `std::mutex` + `std::condition_variable` 阻塞调用线程，直到 `onDone` 回调触发。
- 流结束时向 `callback` 发送 `LLMStreamChunk::done = true` 信号。
- `thinking` 字段对 OpenAI 兼容 provider 始终为空字符串（Extended Thinking 仅 Anthropic 支持）。

**线程注意**：`onChunk` 和 `onDone` 均在 FastNet IO 线程触发，`callback` 也在该线程调用。如需更新 UI，必须通过 `QMetaObject::invokeMethod` 或信号/槽跨线程派发。

### ChatTurnResult — thinking 字段

`src/runtime/RuntimeTypes.h` 中的 `ChatTurnResult` 新增了 `thinking` 字段：

```cpp
struct ChatTurnResult {
    QString content;
    QString thinking;  // model reasoning / extended thinking content
    // ...
};
```

该字段用于将 provider 层的 `LLMResponse::thinking` 透传到运行时层，供 `AgentLoop` 和 Chat 页面消费。非 Extended Thinking 场景下该字段为空字符串，调用方在展示或写入会话记录前应判空。

`RuntimeSerialization.cpp` 中的 `toJson(ChatTurnResult)` 和 `chatTurnResultFromJson()` 均已包含 `thinking` 字段，确保该字段在 JSON 序列化/反序列化（跨进程传输、持久化）时被正确保留。

### ChatPage.qml — thinking 字段透传与展示

`qml/pages/ChatPage.qml` 在构建消息对象时，将 `thinking` 字段从消息模型透传到 QML 消息对象：

```qml
"thinking": typeof thinking !== "undefined" ? thinking : "",
```

这是 `thinking` 字段在整个调用链中的最后一环：`LLMResponse::thinking` → `ChatTurnResult::thinking` → `StudioBackendDto` → QML 消息对象。

#### 可折叠思考过程块

当消息的 `thinking` 字段非空时，Chat 页面在消息气泡内、trace 块之前渲染一个可折叠的"模型思考过程"区域：

- 标题栏（高度 32px，深色背景 `#0D1020`）显示折叠箭头（`▸` / `▾`）和"模型思考过程"标签，点击切换展开/收起状态。
- 展开后显示只读 `TextEdit`，背景色 `#080B18`，文字色 `#7A9BBF`，字号 12px，纯文本格式，支持鼠标选中复制。
- 该块仅对非用户消息（`entry.role !== "user"`）、非 pending 状态、且 `entry.thinking` 有值时可见；非 Extended Thinking 场景下 `thinking` 为空字符串，该块不渲染。

### AgentTurnResult — thinking 字段

`src/agent/AgentLoop.h` 中的 `AgentTurnResult` 结构体包含 `thinking` 字段：

```cpp
struct AgentTurnResult {
    QString content;
    QString thinking;  // model reasoning / extended thinking content
};
```

该结构体是 `AgentLoop` 层的返回类型，与 `ChatTurnResult` 平行，用于将 provider 层的 `LLMResponse::thinking` 透传到 agent 调用方。非 Extended Thinking 场景下 `thinking` 为空字符串，调用方在展示或写入会话记录前应判空。

### AgentLoop — 流式回调 API

`AgentLoop` 提供一个流式进度回调接口：

```cpp
void setStreamProgressCallback(const StreamProgressCallback &cb);
// StreamProgressCallback = std::function<void(const QString &contentDelta, const QString &thinkingDelta)>
```

`AgentLoop` 在调用 `chatStreaming` 时，若 `_streamProgressCallback` 已注册，会在内部构造一个 `LLMStreamCallback` lambda，将每个非终止 chunk 的 `contentDelta` 和 `thinkingDelta` 解包后转发给 `_streamProgressCallback`：

```cpp
_streamProgressCallback
    ? providers::LLMStreamCallback([this](const providers::LLMStreamChunk &chunk) {
          if (!chunk.done && _streamProgressCallback) {
              _streamProgressCallback(chunk.contentDelta, chunk.thinkingDelta);
          }
      })
    : providers::LLMStreamCallback{}
```

- 若未注册 `_streamProgressCallback`，则向 provider 传入空回调（非流式降级）。
- 回调在 FastNet IO 线程触发，调用方负责线程安全。如需更新 UI，必须通过 `QMetaObject::invokeMethod` 或信号/槽跨线程派发。
- `chunk.done == true` 的终止帧不会触发回调，仅用于标记流结束。

## RuntimeCore 公开 API 说明

### invokeProcessDirect 与 invokeProcessDirectDetailed

`RuntimeCore` 提供两个同步调用 agent 处理流程的方法：

```cpp
// 仅返回文本内容（QString）
QString invokeProcessDirect(const QString &content,
                            const QString &sessionKey,
                            const QString &channel,
                            const QString &chatId,
                            const QString &modelOverride = QString(),
                            const QString &providerOverride = QString(),
                            const QJsonObject &runtimeMetadata = QJsonObject());

// 返回完整结果，包含 content 和 thinking 字段
agent::AgentTurnResult invokeProcessDirectDetailed(const QString &content,
                                                   const QString &sessionKey,
                                                   const QString &channel,
                                                   const QString &chatId,
                                                   const QString &modelOverride = QString(),
                                                   const QString &providerOverride = QString(),
                                                   const QJsonObject &runtimeMetadata = QJsonObject());
```

两者参数完全相同，区别在于返回类型：

- `invokeProcessDirect` 返回 `QString`，适合只需要文本回复的调用方（如 CLI `yaos agent`）。
- `invokeProcessDirectDetailed` 返回 `agent::AgentTurnResult`，包含 `content` 和 `thinking` 两个字段，适合需要展示或记录 Extended Thinking 内容的调用方（如 Chat 页面、`RuntimeHttpServer`）。

调用方在使用 `invokeProcessDirectDetailed` 时，应在展示或写入会话记录前判断 `thinking` 是否为空字符串。

## Config API 说明

### 按 ID 查找 Provider 配置

`Config` 类提供两个 `providerById()` 重载，用于通过 snake_case provider ID 直接获取 `ProviderConfig` 指针：

```cpp
ProviderConfig *Config::providerById(const QString &providerId);
const ProviderConfig *Config::providerById(const QString &providerId) const;
```

- 输入 ID 会自动调用 `Config::normalizeProviderId()` 处理：trim、转小写、将 `-` 替换为 `_`，并解析历史别名（如 `azureopenai` → `azure_openai`、`githubcopilot` → `github_copilot`）。
- 找不到时返回 `nullptr`，调用方需判空。
- 与 `matchedProvider()`（返回值拷贝）不同，这两个方法返回指针，可直接修改配置对象。

`Config::normalizeProviderId(const QString &)` 是项目唯一权威的 provider ID normalize 实现，`StudioBridge`、`LocalRuntimeClient` 等所有调用方都应使用此方法，不要自行实现 normalize 逻辑。

## IStudioBackend — 流式进度回调

`IStudioBackend` 接口（`src/ui/StudioBackend.h`）提供一个可选的流式进度回调扩展点：

```cpp
using StreamProgressCallback = std::function<void(const QString &contentDelta, const QString &thinkingDelta)>;
virtual void setStreamProgressCallback(StreamProgressCallback cb) { Q_UNUSED(cb) }
```

- 调用方（如 `StudioBridge`）可在发起流式对话前注册此回调，以实时接收每个增量片段。
- `contentDelta`：本次增量的文本内容；`thinkingDelta`：本次增量的 Extended Thinking 内容。
- 回调在 **FastNet IO 线程**上触发，不在 Qt 主线程。如需更新 UI，必须通过 `QMetaObject::invokeMethod` 或信号/槽跨线程派发。
- 基类提供空实现（`Q_UNUSED(cb)`），不支持流式的后端实现可忽略此方法。
- 该回调与 `processMessageDetailed()` 的返回值互补：回调用于实时推送增量，返回值携带完整的最终结果。

### 实现状态

| 类 | 实现状态 |
|---|---|
| `RuntimeFacadeStudioBackend` | 已实现（`override`），将回调传递给底层 `IRuntimeFacade` 流式调用 |
| `RemoteStudioBackend` | 继承自 `RuntimeFacadeStudioBackend`，行为相同 |
| 其他自定义后端 | 使用基类空实现，不触发回调 |

## IRuntimeFacade — 流式进度回调

`IRuntimeFacade` 接口同样定义了 `setStreamProgressCallback()`，供运行时层直接接受流式回调注册：

```cpp
using StreamProgressCallback = std::function<void(const QString &contentDelta, const QString &thinkingDelta)>;
virtual void setStreamProgressCallback(StreamProgressCallback cb) = 0;
```

### 实现状态

| 类 | 实现状态 |
|---|---|
| `RuntimeCore` | 已实现（`override`），将回调存储并在 `processMessageDetailed()` 流式调用时转发给 provider 层 |
| 其他 `IRuntimeFacade` 实现 | 视具体实现而定，未实现时不触发回调 |

`RuntimeFacadeStudioBackend::setStreamProgressCallback()` 最终调用的就是 `IRuntimeFacade::setStreamProgressCallback()`，因此整条调用链为：

```
StudioBridge
  → IStudioBackend::setStreamProgressCallback()
    → RuntimeFacadeStudioBackend::setStreamProgressCallback()
      → IRuntimeFacade::setStreamProgressCallback()
        → RuntimeCore::setStreamProgressCallback()  ← 本次新增实现
```

## StudioBridge 运行时行为

### 流式进度回调注册

`StudioBridge` 在每次发起聊天请求（`sendMessage` / `sendMessageStreaming`）时，会在启动后台任务之前向当前 `IStudioBackend` 注册流式进度回调：

```cpp
m_backend->setStreamProgressCallback(
    [this](const QString &contentDelta, const QString &thinkingDelta) {
        QMetaObject::invokeMethod(this, [this, contentDelta, thinkingDelta]() {
            applyStreamDelta(contentDelta, thinkingDelta);
        }, Qt::QueuedConnection);
    }
);
```

- 回调在 **FastNet IO 线程**触发，通过 `QMetaObject::invokeMethod(..., Qt::QueuedConnection)` 安全派发回 Qt 主线程。
- 主线程侧调用 `applyStreamDelta(contentDelta, thinkingDelta)`，将增量内容实时写入 Chat 页面消息模型。
- 注册操作受 `m_backendMutex` 保护，确保 backend 指针在多线程环境下安全访问。
- 每次新请求都会重新注册，旧回调自动被覆盖，不存在多次注册叠加的问题。

### Sidecar 自动重连

`StudioBridge` 在每次核心刷新周期（约 7 秒一次）结束后，会检测 sidecar 是否意外断开：

- 条件：`config.normalizedRuntimeMode()` 为 `daemon` 或 `remote`，且刷新返回的 `runtimeMode` 为空字符串。
- 策略：每隔一个刷新周期（约 14 秒）调用一次 `rebuildStudioBackend()`，避免频繁重建。
- 成功时：重置计数器，发出 `success` toast 提示"运行时已恢复"。
- 首次失败时：发出 `warning` toast 提示连接中断，后续失败静默重试。
- 聊天进行中（`m_chatWatcher.isRunning()`）时跳过重连，避免干扰正在进行的请求。

该逻辑位于 `src/ui/StudioBridge.cpp`，在 `maybeCompleteManualRefreshToast()` 末尾执行，不影响正常 local 模式。

## 后续建议

1. 补单元/集成测试：优先覆盖 FastNet HTTP/WebSocket 适配、Email STARTTLS、daemon loopback。
2. 把 `qmake/modules/*.pri` 作为模块边界检查输入，增加脚本扫描禁止 include 关系。
3. 继续收敛 daemon 发布目录，只保留运行必需 DLL 和插件。
