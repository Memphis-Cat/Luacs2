@echo off
setlocal EnableExtensions
cd /d "%~dp0"

set "GAME_ROOT=%~1"
if not defined GAME_ROOT set "GAME_ROOT=%LUACS_GAME_ROOT%"
if not defined GAME_ROOT set "GAME_ROOT=F:\steamcmd2\steamapps\common\Counter-Strike Global Offensive\game\csgo"

set "PACKAGE=%CD%\build\package\game\csgo\addons"
if not exist "%PACKAGE%\LuaCS\bin\win64\luacs2.dll" (
  echo ERROR: Build output is missing. Run build.bat first.
  exit /b 1
)
if not exist "%GAME_ROOT%\addons\metamod" (
  echo ERROR: Metamod folder was not found at "%GAME_ROOT%\addons\metamod".
  exit /b 1
)

robocopy "%PACKAGE%\LuaCS" "%GAME_ROOT%\addons\LuaCS" /E /NFL /NDL /NJH /NJS /NP >nul
if errorlevel 8 exit /b %ERRORLEVEL%
copy /Y "%PACKAGE%\metamod\luacs2.vdf" "%GAME_ROOT%\addons\metamod\luacs2.vdf" >nul || exit /b 1

echo LuaCS deployed to:
echo   %GAME_ROOT%\addons\LuaCS
echo Metamod descriptor:
echo   %GAME_ROOT%\addons\metamod\luacs2.vdf
exit /b 0
