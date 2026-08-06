@echo off
setlocal EnableExtensions
cd /d "%~dp0"

set "GAME_ROOT=%~1"
if not defined GAME_ROOT set "GAME_ROOT=%LUACS_GAME_ROOT%"
if not defined GAME_ROOT set "GAME_ROOT=F:\steamcmd2\steamapps\common\Counter-Strike Global Offensive\game\csgo"

set "PACKAGE=%CD%\build\package\game\csgo\addons"
set "PACKAGE_LUACS=%PACKAGE%\LuaCS"
set "BUILD_STAMP=%PACKAGE_LUACS%\build_commit.txt"

if not exist "%PACKAGE_LUACS%\bin\win64\luacs2.dll" (
  echo ERROR: Build output is missing. Run build.bat first.
  exit /b 1
)

powershell -NoProfile -ExecutionPolicy Bypass -File "tools\build-stamp.ps1" ^
  -Mode Validate -StampPath "%BUILD_STAMP%" -RepositoryRoot "%CD%" || exit /b 1
set "BUILT_COMMIT="
set /p BUILT_COMMIT=<"%BUILD_STAMP%"

if not exist "%GAME_ROOT%\addons\metamod" (
  echo ERROR: Metamod folder was not found at "%GAME_ROOT%\addons\metamod".
  exit /b 1
)

set "CSS_GAMEDATA=%GAME_ROOT%\addons\counterstrikesharp\gamedata\gamedata.json"
set "LUACS_GAMEDATA=%GAME_ROOT%\addons\LuaCS\gamedata\reference\official_windows_gamedata.json"
if exist "%CSS_GAMEDATA%" (
  call :validate_live_gamedata "%CSS_GAMEDATA%" || exit /b 1
) else (
  echo WARNING: CounterStrikeSharp live gamedata was not found.
  echo          LuaCS will use its packaged Windows gamedata snapshot.
)

robocopy "%PACKAGE_LUACS%" "%GAME_ROOT%\addons\LuaCS" /E /NFL /NDL /NJH /NJS /NP >nul
if errorlevel 8 exit /b %ERRORLEVEL%
copy /Y "%PACKAGE%\metamod\luacs2.vdf" "%GAME_ROOT%\addons\metamod\luacs2.vdf" >nul || exit /b 1

if exist "%CSS_GAMEDATA%" (
  copy /Y "%CSS_GAMEDATA%" "%LUACS_GAMEDATA%" >nul || exit /b 1
  echo Imported current CounterStrikeSharp gamedata for this CS2 server build.
)

echo LuaCS deployed commit:
echo   %BUILT_COMMIT%
echo LuaCS deployed to:
echo   %GAME_ROOT%\addons\LuaCS
echo Metamod descriptor:
echo   %GAME_ROOT%\addons\metamod\luacs2.vdf
exit /b 0

:validate_live_gamedata
set "GAMEDATA_FILE=%~1"
for %%K in (
  ClientPrint
  UTIL_ClientPrintAll
  CBasePlayerPawn_RemovePlayerItem
  UTIL_Remove
  CCSPlayerController_SwitchTeam
  CBasePlayerController_SetPawn
) do (
  findstr /L /C:"%%K" "%GAMEDATA_FILE%" >nul || (
    echo ERROR: CounterStrikeSharp gamedata is missing required entry %%K.
    exit /b 1
  )
)
exit /b 0
