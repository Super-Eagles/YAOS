@echo off
setlocal EnableExtensions EnableDelayedExpansion

set "PROJECT_DIR=%~dp0"
if "%PROJECT_DIR:~-1%"=="\" set "PROJECT_DIR=%PROJECT_DIR:~0,-1%"

set "CMAKE_EXE=C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
set "VS_VCVARS=C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\VC\Auxiliary\Build\vcvarsall.bat"
set "BUILD_DIR=%PROJECT_DIR%\build-cmake-nmake"
set "BUILD_TYPE=Debug"
set "TARGETS=yaos yaosd yaos_tests"

:parse_args
if "%~1"=="" goto after_parse
if /I "%~1"=="--release" (
    set "BUILD_TYPE=Release"
    shift
    goto parse_args
)
if /I "%~1"=="--debug" (
    set "BUILD_TYPE=Debug"
    shift
    goto parse_args
)
if /I "%~1"=="--target" goto parse_target
if /I "%~1"=="--build-dir" goto parse_build_dir
if /I "%~1"=="--help" goto show_help
echo [ERROR] Unknown argument: %~1
goto show_help

:parse_target
shift
if "%~1"=="" goto invalid_args
set "TARGETS=%~1"
shift
goto parse_args

:parse_build_dir
shift
if "%~1"=="" goto invalid_args
set "BUILD_DIR=%~1"
shift
goto parse_args

:after_parse
if exist "%CMAKE_EXE%" goto cmake_found
echo [ERROR] cmake not found: %CMAKE_EXE%
exit /b 1

:cmake_found
if exist "%VS_VCVARS%" goto vcvars_found
echo [ERROR] vcvarsall.bat not found: %VS_VCVARS%
exit /b 1

:vcvars_found

call "%VS_VCVARS%" x64 >nul
if errorlevel 1 exit /b 1

"%CMAKE_EXE%" -S "%PROJECT_DIR%" -B "%BUILD_DIR%" -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=%BUILD_TYPE% -DQT_ROOT=C:/Qt/Qt5.14.2/5.14.2/msvc2017_64 -DFASTNET_ROOT=D:/GITHUB/FastNet
if errorlevel 1 exit /b 1

for %%T in (%TARGETS%) do (
    "%CMAKE_EXE%" --build "%BUILD_DIR%" --target %%T
    if errorlevel 1 exit /b 1
)

echo [cmake] OK
exit /b 0

:invalid_args
echo [ERROR] Missing value for previous argument.
exit /b 1

:show_help
echo Usage: cmake_build.bat [--debug^|--release] [--target "yaos yaosd yaos_tests"] [--build-dir DIR]
exit /b 0
