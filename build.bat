@echo off
setlocal EnableExtensions EnableDelayedExpansion

set "PROJECT_DIR=%~dp0"
if "%PROJECT_DIR:~-1%"=="\" set "PROJECT_DIR=%PROJECT_DIR:~0,-1%"

set "QT_DIR=C:\Qt\Qt5.14.2\5.14.2\msvc2017_64"
set "QMAKE_EXE="
set "VS_VCVARS="
set "BUILD_RELEASE=1"
set "BUILD_DEBUG=0"
set "CLEAN_FIRST=0"
set "CLEAN_ONLY=0"
set "RUN_CHECKS=1"
set "RUN_RUNTIME_LAYOUT_CHECK=1"
set "CHECK_ONLY=0"
set "SKIP_GUI=0"
set "SKIP_DAEMON=0"
set "BUILD_TESTS=0"

:parse_args
if "%~1"=="" goto after_parse
if /I "%~1"=="--help" goto show_help
if /I "%~1"=="--clean" (
    set "CLEAN_ONLY=1"
    shift
    goto parse_args
)
if /I "%~1"=="--rebuild" (
    set "CLEAN_FIRST=1"
    shift
    goto parse_args
)
if /I "%~1"=="--no-check" (
    set "RUN_CHECKS=0"
    shift
    goto parse_args
)
if /I "%~1"=="--no-runtime-check" (
    set "RUN_RUNTIME_LAYOUT_CHECK=0"
    shift
    goto parse_args
)
if /I "%~1"=="--check-only" (
    set "CHECK_ONLY=1"
    shift
    goto parse_args
)
if /I "%~1"=="--skip-gui" (
    set "SKIP_GUI=1"
    shift
    goto parse_args
)
if /I "%~1"=="--skip-daemon" (
    set "SKIP_DAEMON=1"
    shift
    goto parse_args
)
if /I "%~1"=="--with-tests" (
    set "BUILD_TESTS=1"
    shift
    goto parse_args
)
if /I "%~1"=="--tests-only" (
    set "BUILD_TESTS=1"
    set "SKIP_GUI=1"
    set "SKIP_DAEMON=1"
    shift
    goto parse_args
)
if /I "%~1"=="--release-only" (
    set "BUILD_RELEASE=1"
    set "BUILD_DEBUG=0"
    shift
    goto parse_args
)
if /I "%~1"=="--debug-only" (
    set "BUILD_RELEASE=0"
    set "BUILD_DEBUG=1"
    shift
    goto parse_args
)
if /I "%~1"=="--qt-dir" goto parse_qt_dir
if /I "%~1"=="--vcvars" goto parse_vcvars

echo [ERROR] Unknown argument: %~1
goto show_help

:parse_qt_dir
shift
if "%~1"=="" goto invalid_args
set "QT_DIR=%~1"
if "!QT_DIR:~-1!"=="\" set "QT_DIR=!QT_DIR:~0,-1!"
shift
goto parse_args

:parse_vcvars
shift
if "%~1"=="" goto invalid_args
set "VS_VCVARS=%~1"
shift
goto parse_args

:after_parse
set "QMAKE_EXE=%QT_DIR%\bin\qmake.exe"

echo ========================================
echo   YAOS Build Script
echo ========================================
echo Project dir : %PROJECT_DIR%
echo Qt dir      : %QT_DIR%
if "%RUN_CHECKS%"=="1" (
    echo Checks      : enabled
) else (
    echo Checks      : skipped
)
if "%RUN_RUNTIME_LAYOUT_CHECK%"=="1" (
    echo Runtime chk : enabled
) else (
    echo Runtime chk : skipped
)

if not exist "%QMAKE_EXE%" (
    echo [ERROR] qmake not found: "%QMAKE_EXE%"
    exit /b 1
)

if "%SKIP_GUI%"=="1" if "%SKIP_DAEMON%"=="1" if not "%BUILD_TESTS%"=="1" (
    echo [ERROR] --skip-gui and --skip-daemon cannot be used together unless --with-tests or --tests-only is set.
    exit /b 1
)

if "%CLEAN_ONLY%"=="1" (
    echo [clean] Removing qmake build outputs...
    if exist "%PROJECT_DIR%\build" rmdir /s /q "%PROJECT_DIR%\build"
    if exist "%PROJECT_DIR%\lib" rmdir /s /q "%PROJECT_DIR%\lib"
    if exist "%PROJECT_DIR%\release" rmdir /s /q "%PROJECT_DIR%\release"
    if exist "%PROJECT_DIR%\debug" rmdir /s /q "%PROJECT_DIR%\debug"
    del /q "%PROJECT_DIR%\Makefile" "%PROJECT_DIR%\Makefile.*" "%PROJECT_DIR%\Makefile.yaos_base*" "%PROJECT_DIR%\Makefile.yaos_business*" "%PROJECT_DIR%\Makefile.yaosd*" >nul 2>nul
    echo [clean] Completed.
    exit /b 0
)

if "%RUN_CHECKS%"=="1" (
    call :run_architecture_check
    if errorlevel 1 exit /b 1
)

if "%CHECK_ONLY%"=="1" (
    echo [check] Completed.
    exit /b 0
)

if not defined VS_VCVARS (
    call :detect_visual_studio
    if errorlevel 1 exit /b 1
)

echo Toolchain  : %VS_VCVARS%
call "%VS_VCVARS%" x64 >nul
if errorlevel 1 (
    echo [ERROR] Failed to initialize MSVC x64 environment.
    exit /b 1
)

if "%CLEAN_FIRST%"=="1" (
    echo [clean] Removing qmake build outputs...
    if exist "%PROJECT_DIR%\build" rmdir /s /q "%PROJECT_DIR%\build"
    if exist "%PROJECT_DIR%\lib" rmdir /s /q "%PROJECT_DIR%\lib"
    if exist "%PROJECT_DIR%\release" rmdir /s /q "%PROJECT_DIR%\release"
    if exist "%PROJECT_DIR%\debug" rmdir /s /q "%PROJECT_DIR%\debug"
    del /q "%PROJECT_DIR%\Makefile" "%PROJECT_DIR%\Makefile.*" "%PROJECT_DIR%\Makefile.yaos_base*" "%PROJECT_DIR%\Makefile.yaos_business*" "%PROJECT_DIR%\Makefile.yaosd*" >nul 2>nul
)

if "%BUILD_RELEASE%"=="1" (
    call :build_config release Release
    if errorlevel 1 exit /b 1
)

if "%BUILD_DEBUG%"=="1" (
    call :build_config debug Debug
    if errorlevel 1 exit /b 1
)

if "%RUN_RUNTIME_LAYOUT_CHECK%"=="1" (
    if "%SKIP_GUI%"=="1" (
        echo [runtime-layout] Skipped because --skip-gui was used.
    ) else if "%SKIP_DAEMON%"=="1" (
        echo [runtime-layout] Skipped because --skip-daemon was used.
    ) else (
        call :run_runtime_layout_check
        if errorlevel 1 exit /b 1
    )
)

echo ========================================
echo   YAOS build completed successfully
echo ========================================
echo Runtime dir : %PROJECT_DIR%\bin
echo Debug dir   : %PROJECT_DIR%\bin\debug
echo Library dir : %PROJECT_DIR%\lib
exit /b 0

:build_config
set "CONFIG_NAME=%~1"
set "MAKE_SUFFIX=%~2"

echo ----------------------------------------
echo Building %MAKE_SUFFIX%
echo ----------------------------------------

call :qmake_target yaos_base "%PROJECT_DIR%\yaos_base.pro" "%PROJECT_DIR%\Makefile.yaos_base" %CONFIG_NAME% %MAKE_SUFFIX%
if errorlevel 1 exit /b 1

call :qmake_target yaos_business "%PROJECT_DIR%\yaos_business.pro" "%PROJECT_DIR%\Makefile.yaos_business" %CONFIG_NAME% %MAKE_SUFFIX%
if errorlevel 1 exit /b 1

if "%SKIP_GUI%"=="1" goto after_gui_target
call :qmake_target yaos "%PROJECT_DIR%\YAOS.pro" "%PROJECT_DIR%\Makefile" %CONFIG_NAME% %MAKE_SUFFIX%
if errorlevel 1 exit /b 1
:after_gui_target

if "%SKIP_DAEMON%"=="1" goto after_daemon_target
call :qmake_target yaosd "%PROJECT_DIR%\yaosd.pro" "%PROJECT_DIR%\Makefile.yaosd" %CONFIG_NAME% %MAKE_SUFFIX%
if errorlevel 1 exit /b 1
:after_daemon_target

if not "%BUILD_TESTS%"=="1" goto after_tests_target
call :qmake_target yaos_tests "%PROJECT_DIR%\yaos_tests.pro" "%PROJECT_DIR%\Makefile.yaos_tests" %CONFIG_NAME% %MAKE_SUFFIX%
if errorlevel 1 exit /b 1
:after_tests_target

exit /b 0

:run_architecture_check
echo [check] Architecture boundaries
powershell -NoProfile -ExecutionPolicy Bypass -File "%PROJECT_DIR%\scripts\check_architecture.ps1" -ProjectRoot "%PROJECT_DIR%"
if errorlevel 1 (
    echo [ERROR] Architecture check failed.
    exit /b 1
)
exit /b 0

:run_runtime_layout_check
set "RUNTIME_LAYOUT_CONFIG=all"
if "%BUILD_RELEASE%"=="1" if "%BUILD_DEBUG%"=="0" set "RUNTIME_LAYOUT_CONFIG=release"
if "%BUILD_RELEASE%"=="0" if "%BUILD_DEBUG%"=="1" set "RUNTIME_LAYOUT_CONFIG=debug"
echo [check] Runtime layout %RUNTIME_LAYOUT_CONFIG%
powershell -NoProfile -ExecutionPolicy Bypass -File "%PROJECT_DIR%\scripts\check_runtime_layout.ps1" -ProjectRoot "%PROJECT_DIR%" -Config "%RUNTIME_LAYOUT_CONFIG%"
if errorlevel 1 (
    echo [ERROR] Runtime layout check failed.
    exit /b 1
)
exit /b 0

:qmake_target
set "TARGET_NAME=%~1"
set "PRO_FILE=%~2"
set "MAKEFILE_BASE=%~3"
set "CONFIG_NAME=%~4"
set "MAKE_SUFFIX=%~5"

echo [%CONFIG_NAME%] qmake %TARGET_NAME%
if /I "%CONFIG_NAME%"=="debug" (
    "%QMAKE_EXE%" -o "%MAKEFILE_BASE%" "%PRO_FILE%" -spec win32-msvc "CONFIG+=debug" "CONFIG-=release"
) else (
    "%QMAKE_EXE%" -o "%MAKEFILE_BASE%" "%PRO_FILE%" -spec win32-msvc "CONFIG+=release" "CONFIG-=debug"
)
if errorlevel 1 (
    echo [ERROR] qmake failed: %TARGET_NAME% %CONFIG_NAME%
    exit /b 1
)

echo [%CONFIG_NAME%] nmake %TARGET_NAME%
nmake /f "%MAKEFILE_BASE%.%MAKE_SUFFIX%"
if errorlevel 1 (
    echo [ERROR] nmake failed: %TARGET_NAME% %CONFIG_NAME%
    exit /b 1
)
exit /b 0

:detect_visual_studio
call :try_vcvars "C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\VC\Auxiliary\Build\vcvarsall.bat"
if defined VS_VCVARS exit /b 0
call :try_vcvars "C:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools\VC\Auxiliary\Build\vcvarsall.bat"
if defined VS_VCVARS exit /b 0
call :try_vcvars "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat"
if defined VS_VCVARS exit /b 0
call :try_vcvars "C:\Program Files\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat"
if defined VS_VCVARS exit /b 0

echo [ERROR] Visual Studio 2019/2022 x64 toolchain not found.
echo         Pass --vcvars "path\to\vcvarsall.bat" if it is installed elsewhere.
exit /b 1

:try_vcvars
if exist "%~1" set "VS_VCVARS=%~1"
exit /b 0

:invalid_args
echo [ERROR] Missing value for previous argument.
exit /b 1

:show_help
echo Usage: build.bat [options]
echo.
echo Options:
echo   --clean         Remove qmake build/lib outputs and exit (clean only)
echo   --rebuild       Remove qmake build/lib outputs and then build
echo   --no-check      Skip architecture boundary checks
echo   --no-runtime-check
echo                   Skip runtime directory/dependency checks
echo   --check-only    Run architecture boundary checks and exit
echo   --skip-gui      Build libraries and yaosd only
echo   --skip-daemon   Build libraries and yaos only
echo   --with-tests    Also build integration smoke tests
echo   --tests-only    Build libraries and integration smoke tests only
echo   --release-only  Build Release only
echo   --debug-only    Build Debug only
echo   --qt-dir DIR    Override Qt 5.14.2 msvc2017_64 directory
echo   --vcvars PATH   Override Visual Studio vcvarsall.bat path
echo   --help          Show this help
exit /b 0
