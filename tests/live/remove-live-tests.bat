@echo off
setlocal EnableExtensions

set "GAME_ROOT=%~1"
if not defined GAME_ROOT set "GAME_ROOT=F:\steamcmd2\steamapps\common\Counter-Strike Global Offensive\game\csgo"

set "LUACS=%GAME_ROOT%\addons\LuaCS"
set "SCRIPTING=%LUACS%\scripting"
set "PLUGINS=%LUACS%\plugins"

for %%N in (
    apitest
    apitest_survivor
    apitest_callback_crash
    apitest_startup_crash
    apitest_unload_crash
    apitest_disconnect_race
) do (
    del /Q "%SCRIPTING%\%%N.lua" 2>nul
    del /Q "%PLUGINS%\%%N.smg" 2>nul
)

echo LuaCS live test sources/packages removed.
exit /b 0
