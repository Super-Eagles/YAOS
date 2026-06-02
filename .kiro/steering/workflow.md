# Development Workflows

Common step-by-step patterns for this codebase. Follow these exactly to avoid breaking module boundaries or build rules.

## Adding a new LLM Provider

1. Create `src/providers/<Name>Provider.{h,cpp}` implementing `LLMProvider`
2. Register in `src/providers/ProviderRegistry.cpp` with a `snake_case` ID
3. Register in `src/providers/ProviderFactory.cpp` so it can be instantiated
4. Add both files to `qmake/modules/business.pri`
5. If OAuth is needed, extend `ProviderOAuth`
6. Surface config fields in `src/config/Config.h` + `ConfigLoader.cpp`
7. Add a GUI entry in `qml/pages/ModelsPage.qml`

## Adding a new Channel

1. Create `src/channels/<Name>Channel.{h,cpp}` implementing `Channel` + `ChannelHttp` if HTTP-based
2. Register in `src/channels/ChannelManager.cpp`
3. Add both files to `qmake/modules/business.pri`
4. Add backend slot in `src/ui/StudioBackend.cpp` and expose via `StudioBackendDto`
5. Add QML config card in `qml/pages/ChannelsPage.qml`
6. Network calls must go through `FastNetHttpTransport` or `FastNetWebSocketTransport` — never `QNetworkAccessManager`

## Adding a new Agent Tool

1. Create `src/agent/tools/<ToolName>.{h,cpp}` implementing `Tool`
2. Register in `src/agent/ToolRegistry.cpp`
3. Add both files to `qmake/modules/business.pri`
4. Set default policy: `allow` for read-only, `confirm` for side-effects, `deny` for exec
5. Document the tool's input/output schema in a comment on the class

## Adding a new QML Page

1. Create `qml/pages/<Name>Page.qml`
2. Add it to `qml/qml.qrc`
3. Register a nav entry in `qml/Main.qml`
4. Add a backend method in `src/ui/StudioBackend.{h,cpp}` and wire through `StudioBridge`
5. Expose data via `StudioBackendDto` — no direct C++ object exposure to QML

## Adding a new Config Field

1. Add the field to `src/config/Config.h`
2. Load/save it in `src/config/ConfigLoader.cpp`
3. Surface it in the relevant Studio page QML
4. If it affects daemon behavior, ensure `yaosd` reads it via `ApplicationController`

## Adding a source file to a module

Every new `.cpp`/`.h` must be added to the matching `.pri`:

| Layer | .pri file |
|---|---|
| base | `qmake/modules/base.pri` |
| business | `qmake/modules/business.pri` |
| frontend (GUI) | `qmake/modules/frontend.pri` |
| daemon | `qmake/modules/daemon.pri` |

CMake picks up the same `.pri` files automatically — do not add files to `CMakeLists.txt` manually.

## Before committing

```bat
REM Run architecture boundary check
powershell -File scripts/check_architecture.ps1

REM Run runtime layout check (requires a prior build)
powershell -File scripts/check_runtime_layout.ps1

REM Or run both via build.bat
build.bat --check-only
```

Both checks must pass. Never use `--no-check` in a commit.

## Debugging a build failure

1. Check `build_out.txt` for the last build log
2. Use `getDiagnostics` in Kiro for syntax/type errors before rebuilding
3. For linker errors, verify the file is in the correct `.pri`
4. For "unresolved external" on Qt symbols, check `QT +=` lines in the `.pro` file
5. For architecture violations, run `check_architecture.ps1` to see which include is illegal
