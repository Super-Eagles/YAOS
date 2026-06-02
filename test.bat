@echo off
setlocal EnableExtensions

set "PROJECT_DIR=%~dp0"
if "%PROJECT_DIR:~-1%"=="\" set "PROJECT_DIR=%PROJECT_DIR:~0,-1%"

powershell -NoProfile -ExecutionPolicy Bypass -File "%PROJECT_DIR%\scripts\run_integration_tests.ps1" %*
exit /b %ERRORLEVEL%
