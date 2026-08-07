@echo off
setlocal EnableExtensions

set "REPO_ROOT=%~dp0\..\.."
set "GAME_ROOT=%~1"
set "MODE=%~2"

if not defined GAME_ROOT set "GAME_ROOT=F:\steamcmd2\steamapps\common\Counter-Strike Global Offensive\game\csgo"

set "LUACS=%GAME_ROOT%\addons\LuaCS"
set "SCRIPTING=%LUACS%\scripting"
set "PLUGINS=%LUACS%\plugins"
set "COMPILER=%SCRIPTING%\compile.exe"
set "LIVE=%REPO_ROOT%\tests\live"

if not exist "%COMPILER%" (
    echo ERROR: LuaCS compiler was not found:
    echo   %COMPILER%
    echo Deploy/build LuaCS first, or pass the correct CS2 game\csgo path.
    exit /b 1
)

if not exist "%LIVE%\apitest.lua" (
    echo ERROR: live test source is missing:
    echo   %LIVE%\apitest.lua
    exit /b 1
)

if not exist "%SCRIPTING%" mkdir "%SCRIPTING%"
if errorlevel 1 exit /b 1
if not exist "%PLUGINS%" mkdir "%PLUGINS%"
if errorlevel 1 exit /b 1

call :install_one apitest.lua
if errorlevel 1 exit /b 1

if /I "%MODE%"=="isolation" (
    call :install_one apitest_survivor.lua
    if errorlevel 1 exit /b 1
    call :install_one apitest_callback_crash.lua
    if errorlevel 1 exit /b 1
    call :install_one apitest_startup_crash.lua
    if errorlevel 1 exit /b 1
    call :install_one apitest_unload_crash.lua
    if errorlevel 1 exit /b 1
)

echo.
echo LuaCS live test package installed successfully.
echo.
echo Server console:
echo   lua plugins load apitest
echo.
echo Player chat:
echo   !lua_test help
echo   !lua_test all

echo.
if /I "%MODE%"=="isolation" (
    echo Isolation packages were also compiled.
    echo Load survivor and callback crash with:
    echo   lua plugins load apitest_survivor
    echo   lua plugins load apitest_callback_crash
    echo Then in player chat:
    echo   !survivor
    echo   !test_boom
    echo   !survivor
    echo.
    echo Startup-crash is expected to FAIL when loaded:
    echo   lua plugins load apitest_startup_crash
    echo.
    echo Unload-crash is expected to refuse a normal unload:
    echo   lua plugins load apitest_unload_crash
    echo   lua plugins unload apitest_unload_crash
    echo   lua plugins force_unload apitest_unload_crash
)
exit /b 0

:install_one
set "NAME=%~1"
echo.
echo Installing %NAME%...
copy /Y "%LIVE%\%NAME%" "%SCRIPTING%\%NAME%" >nul
if errorlevel 1 (
    echo ERROR: could not copy %NAME% into LuaCS scripting.
    exit /b 1
)
"%COMPILER%" --no-pause "%SCRIPTING%\%NAME%"
if errorlevel 1 (
    echo ERROR: compile failed for %NAME%.
    exit /b 1
)
if not exist "%PLUGINS%\%~n1.smg" (
    echo ERROR: compiler did not create %PLUGINS%\%~n1.smg
    exit /b 1
)
exit /b 0
