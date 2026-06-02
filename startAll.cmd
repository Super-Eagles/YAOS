@echo off
setlocal

set ROOT=%~dp0
set EXE=%ROOT%bin\yaos.exe

if not exist "%EXE%" (
  echo [FAIL] yaos executable not found at %EXE%
  echo Build first, then rerun this script.
  exit /b 1
)

"%EXE%" gateway
