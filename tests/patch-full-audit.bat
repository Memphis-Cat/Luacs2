@echo off
setlocal EnableExtensions
chcp 65001 >nul
set "PYTHONUTF8=1"
set "PYTHONIOENCODING=utf-8"

pushd "%~dp0.." || exit /b 1

call "%CD%\tests\all-smoke.bat"
if errorlevel 1 goto :fail

powershell -NoProfile -ExecutionPolicy Bypass -File "%CD%\tests\invoke-utf8.ps1" -Script "%CD%\tests\all-lua-sources-smoke.ps1"
if errorlevel 1 goto :fail

powershell -NoProfile -ExecutionPolicy Bypass -File "%CD%\tests\invoke-utf8.ps1" -Script "%CD%\tests\final-patch-gate.ps1"
if errorlevel 1 goto :fail

echo.
echo LuaCS full patch audit passed.
popd
exit /b 0

:fail
set "ERR=%ERRORLEVEL%"
echo.
echo LuaCS full patch audit failed with exit code %ERR%.
popd
exit /b %ERR%
