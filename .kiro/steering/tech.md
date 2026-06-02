# Tech Stack

## Languages and toolchain

- C++17, `/std:c++17`, MSVC only
- MSVC 2017 64-bit toolchain via Visual Studio 2019/2022 `vcvarsall.bat x64`
- Qt 5.14.2 `msvc2017_64` at `C:\Qt\Qt5.14.2\5.14.2\msvc2017_64` (override with `--qt-dir` / `-DQT_ROOT`)
- QML + Qt Quick Controls 2 (Fusion style) for the GUI
- FastNet (OpenSSL 3) is the mandatory network transport, expected at `../FastNet` relative to the repo root (override with `FASTNET_ROOT`)
- SQLite via `QtSql` for memory and conversation stores
- PowerShell for check scripts, batch for build entry points

## Qt modules

- `yaos_base` / `yaos_business`: `QT += core concurrent sql`, **`QT -= gui`** (hard rule)
- `yaos` (GUI): adds `qml quick quickcontrols2 gui`
- `yaosd` (daemon): `QT -= gui`; dumpbin must not show QtGui/QtConcurrent linkage
- Never add `QT += network` anywhere. All HTTP/WebSocket/TLS goes through `src/platform/network/FastNet*Transport.*`

## Build systems

Two parallel build systems coexist; qmake is authoritative for release, CMake is being brought up.

### qmake (primary)

Project files build in this fixed order:

1. `yaos_base.pro` -> `lib\[debug\]yaos_base[d].lib`
2. `yaos_business.pro` -> `lib\[debug\]yaos_business[d].lib`
3. `YAOS.pro` -> `bin\[debug\]yaos.exe`
4. `yaosd.pro` -> `bin\[debug\]yaosd.exe`
5. `yaos_tests.pro` -> `bin\[debug\]yaos_tests.exe` (optional)

Source lists live in `qmake/modules/{base,business,frontend,daemon}.pri` and act as the module boundary contract.

### CMake (parallel, for future migration)

`CMakeLists.txt` reads the same `.pri` files via `yaos_read_pri_paths()` so both systems share one source-of-truth. Do not duplicate file lists.

## Common commands (Windows `cmd.exe`)

Always run from the repo root. Scripts assume MSVC x64.

```bat
REM Full build (Debug + Release, libs + GUI + daemon, with checks)
build.bat

REM Debug only, skip architecture and runtime-layout checks
build.bat --debug-only --no-check --no-runtime-check

REM Clean rebuild
build.bat --clean

REM Build integration smoke tests
build.bat --with-tests
build.bat --tests-only --debug-only

REM UI changes only
build.bat --debug-only --skip-daemon

REM Daemon only
build.bat --debug-only --skip-gui

REM Run checks without building
build.bat --check-only

REM CMake/NMake bring-up (optional)
cmake_build.bat --debug
cmake_build.bat --release --target "yaos yaosd yaos_tests"

REM Integration smoke tests
test.bat -Config all

REM GUI regression harness
gui_regression.bat -Config debug -Case runtime-page
```

Prefer `getDiagnostics` over running the build for syntax/type checks. Do not launch `yaos.exe` or long-running services (`gateway`, `*-service`) from automation, run them manually.

## Mandatory pre-build checks

`build.bat` runs two PowerShell guards by default. Both must pass before merging:

- `scripts/check_architecture.ps1` - enforces module boundaries (no QtGui, QtNetwork, or UI includes in base/business; no provider/channel/agent includes in base)
- `scripts/check_runtime_layout.ps1` - verifies `bin\` and `bin\debug\` have the right FastNet/OpenSSL/Qt DLLs and do not mix Debug/Release runtimes

Use `--no-check` / `--no-runtime-check` only for local experiments, never in a commit.

## Output layout

- Release binaries: `bin\`
- Debug binaries: `bin\debug\`
- Release static libs: `lib\`
- Debug static libs: `lib\debug\`
- qmake intermediates: `build\`
- CMake builds: `build-cmake\`, `build-cmake-nmake\`

`build.bat --clean` removes `build\`, `lib\`, and generated `Makefile*`.

## Coding conventions

- C++17, RAII, no raw `new`/`delete` in new code
- Qt-style `Q_OBJECT` + signals/slots for cross-thread communication
- Network: FastNet adapters only. Do not reintroduce `QTcpSocket`, `QSslSocket`, `QLocalSocket`, `QNetworkAccessManager`, `QWebSocket`
- No `QTextDocument` / `QtGui` includes in `yaos_base` or `yaos_business`
- Logs go through `src/runtime/StructuredLog.*` and land in `<workspace>/runtime/logs/<service>-<pid>.jsonl`
- JSON is `QJsonDocument` / `QJsonObject`; provider IDs and CLI keys use `snake_case`
- QML style is `Fusion` (set in `main.cpp`); do not switch to `Basic`
