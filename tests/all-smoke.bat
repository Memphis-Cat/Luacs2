@echo off
setlocal EnableExtensions
cd /d "%~dp0\.."

powershell -NoProfile -ExecutionPolicy Bypass -File ".\tests\all-smoke.ps1"
exit /b %ERRORLEVEL%
