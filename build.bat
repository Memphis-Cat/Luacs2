@echo off
setlocal EnableExtensions
cd /d "%~dp0"

set "NO_DEPLOY="
if /I "%~1"=="--no-deploy" (
  set "NO_DEPLOY=1"
  shift
)

set "GAME_ROOT=%LUACS_GAME_ROOT%"
if not "%~1"=="" set "GAME_ROOT=%~1"
if not defined GAME_ROOT set "GAME_ROOT=F:\steamcmd2\steamapps\common\Counter-Strike Global Offensive\game\csgo"

where git >nul 2>nul || (
  echo ERROR: Git is required to fetch Metamod and HL2SDK.
  exit /b 1
)
where cmake >nul 2>nul || (
  echo ERROR: CMake 3.26 or newer is required.
  exit /b 1
)
where ninja >nul 2>nul || (
  echo ERROR: Ninja is required. Run this from a Visual Studio Developer Command Prompt with Ninja installed.
  exit /b 1
)

powershell -NoProfile -ExecutionPolicy Bypass -File "tools\restore-reference.ps1" || exit /b 1

if not exist "deps\metamod-source\core\ISmmPlugin.h" (
  echo Fetching Metamod:Source...
  git clone --depth 1 --branch master https://github.com/alliedmodders/metamod-source.git "deps\metamod-source" || exit /b 1
)
if not exist "deps\hl2sdk-cs2\public\iserver.h" (
  echo Fetching AlliedModders HL2SDK cs2 branch...
  git clone --depth 1 --branch cs2 https://github.com/alliedmodders/hl2sdk.git "deps\hl2sdk-cs2" || exit /b 1
)

cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release ^
  -DMMSOURCE_ROOT="%CD%\deps\metamod-source" ^
  -DHL2SDK_CS2_ROOT="%CD%\deps\hl2sdk-cs2" || exit /b 1
cmake --build build --target luacs_package || exit /b 1

if defined NO_DEPLOY (
  echo LuaCS build and package completed without deployment.
  exit /b 0
)

call deploy.bat "%GAME_ROOT%"
exit /b %ERRORLEVEL%
