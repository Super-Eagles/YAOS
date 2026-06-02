# Project Structure

## Top-level layout

```
YAOS/
  src/                      C++ sources (see layering below)
  qml/                      QML front-end (Main, Startup, pages/, components/, logic/, theme/)
  qmake/modules/            .pri source lists that define module boundaries
  tests/                    Integration smoke tests (currently IntegrationSmoke.cpp)
  scripts/                  PowerShell checks and patch helpers
  docs/                     Architecture and review notes
  assets/                   Icons and marketing images
  bin/                      Build output - release exes and required DLLs
  bin/debug/                Build output - debug exes and debug DLLs
  lib/                      Release static libs
  lib/debug/                Debug static libs
  build/                    qmake intermediates
  build-cmake/              CMake intermediates
  build-cmake-nmake/        CMake + NMake intermediates
  .kiro/                    Steering and specs for the AI assistant
  YAOS.pro                  GUI executable (yaos.exe)
  yaosd.pro                 Headless daemon (yaosd.exe)
  yaos_base.pro             Foundation static lib
  yaos_business.pro         Business static lib
  yaos_tests.pro            Optional integration smoke test exe
  CMakeLists.txt            Parallel CMake build (reads the same .pri files)
  build.bat                 Main Windows build entry (qmake + nmake)
  cmake_build.bat           Parallel CMake build entry
  test.bat, gui_regression.bat
  startAll.cmd              Launches bin\yaos.exe gateway
```

## Architecture layering (enforced)

Four layers, each backed by a `.pri` file in `qmake/modules/`:

### 1. Base - `qmake/modules/base.pri` -> `yaos_base.lib`

Infrastructure only, **no Qt Gui, no business logic**.

- `src/config/` - `Config`, `ConfigLoader`
- `src/platform/network/` - `FastNetHttpTransport`, `FastNetWebSocketTransport` (the only network layer)
- `src/daemon/` loopback protocol (`LocalDaemonProtocol`, `LocalDaemonServer`)
- `src/distributed/` - node registry, task bus, P2P cluster, remote control client
- `src/bus/` - in-process `MessageBus`
- `src/session/` - `SessionManager`

Forbidden includes: `src/ui/*`, `src/agent/*`, `src/providers/*`, `src/channels/*`, `src/runtime/*`, any `QtGui`/`QtNetwork`.

### 2. Business - `qmake/modules/business.pri` -> `yaos_business.lib`

Runtime and product features that are independent of Qt Quick.

- `src/runtime/` - `RuntimeCore`, `RuntimeFacade`, `RuntimeHttpServer`, `ApprovalStore`, `AutomationStore`, `CronService`, `EventLog`, `HeartbeatService`, `NotificationCenter`, `PluginRegistry`, `SkillRegistry`, `MCPManager`, `ResourceCatalog`, `StructuredLog`, `SubagentManager`, `TaskStore`, `Templates`, Local/Remote/Facade runtime clients
- `src/memory/` - layered exporter, ingestor, retriever, SQLite stores, `MemoryHttpServer`, `MemoryServiceCore`, remote memory client/protocol
- `src/providers/` - `LLMProvider`, `ProviderFactory`, `ProviderRegistry`, `ProviderOAuth`, Anthropic/OpenAI-compatible/Echo providers
- `src/channels/` - all channel implementations (Telegram/Slack/Feishu/DingTalk/Discord/Matrix/Email/WhatsApp/QQ/Mochat) and `ChannelManager`
- `src/agent/` - `AgentLoop`, `ContextBuilder`, `ToolRegistry`, `MemoryStore`, tool implementations under `src/agent/tools/`
- `src/control/` - control plane glue
- `src/app/ApplicationController.*` - shared between GUI and daemon entry points (this is the "business app" layer, not UI)

Constraints: `QT -= gui`; network calls must route through FastNet adapters; no `QTextDocument` or `QtGui` symbols.

### 3. Front-end - `qmake/modules/frontend.pri` -> `yaos.exe`

Qt Quick GUI entry only.

- `src/main.cpp` - CLI dispatch + GUI bootstrap (decides between `gui`, `gateway`, `daemon`, `runtime-service`, `memory-service`, `control-service`, `agent`, `gui-regression`, etc.)
- `src/ui/` - `StudioWindow`, `StudioBridge`, `StudioBackend` (+ `_p.h`, `Dto`, `Types`), `RemoteStudioBackend`, `StudioAutomationBackend`, `StudioControlBackend`, `StudioOAuthBackend`, `StudioProviderBackend`, `OAuthLoopbackServer`, `GuiRegressionRunner`
- `qml/` - QML pages and components, bundled through `qml/qml.qrc`

### 4. Daemon - `qmake/modules/daemon.pri` -> `yaosd.exe`

- `src/daemon/DaemonMain.cpp` only. Everything else comes from `yaos_business` + `yaos_base`.

## QML layout

```
qml/
  Main.qml, Startup.qml      top-level scenes
  pages/                     Studio pages (Overview, Models, Chat, Runtime, Security, Channels, Extensions, Automation, Resources)
  components/                reusable QML widgets
  logic/                     QML-side helpers
  theme/                     colors, typography
  qml.qrc                    compiled into yaos.exe
```

QML pages bind to C++ via `StudioBridge` / `StudioBackend`. When adding a page, register the backend slot on the bridge and expose data through `StudioBackendDto`.

## Files to update together

- Adding a `.cpp`/`.h` to `src/<layer>/` -> add it to the matching `qmake/modules/*.pri`. CMake picks it up automatically.
- Adding a provider -> update `ProviderRegistry.cpp` **and** `ProviderFactory.cpp`, keep IDs `snake_case`.
- Adding a channel -> implement `Channel` interface, register in `ChannelManager`, add GUI hooks in `StudioBackend` + QML `Channels` page.
- Adding a tool -> implement `Tool` interface in `src/agent/tools/`, register in `ToolRegistry`, set a default policy (prefer `confirm` or `deny` for anything with side effects).
- Adding config fields -> update `src/config/Config.{h,cpp}` and `ConfigLoader`, and surface them in the relevant Studio page.

## Spec and hook conventions

- Specs live under `.kiro/specs/<feature-name>/` with `requirements.md`, `design.md`, `tasks.md` (or `bugfix.md` for bugfix specs).
- Steering files live under `.kiro/steering/` and are always included unless front-matter opts into `fileMatch` / `manual` inclusion.
- Generated Chinese planning docs (e.g. `执行计划.md`) and old READMEs (`README - 副本.md`) are reference-only, do not treat them as authoritative.
