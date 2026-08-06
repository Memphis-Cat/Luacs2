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
  echo ERROR: Git is required to fetch Metamod, HL2SDK, and CS2 protobufs.
  exit /b 1
)
where cmake >nul 2>nul || (
  echo ERROR: CMake 3.26 or newer is required.
  exit /b 1
)
where ninja >nul 2>nul || (
  echo ERROR: Ninja is required.
  exit /b 1
)
where cl >nul 2>nul || (
  echo ERROR: The 64-bit Microsoft Visual C++ compiler is required.
  echo Run this from an x64 Visual Studio Developer Command Prompt.
  exit /b 1
)

set "CC=cl"
set "CXX=cl"

powershell -NoProfile -ExecutionPolicy Bypass -File "tools\restore-reference.ps1" || exit /b 1

call :fetch_dependency "deps\metamod-source" "https://github.com/alliedmodders/metamod-source.git" "07c708a59e255243bd18f2b807efce6a9aa5ccaf" "core\ISmmPlugin.h" "Metamod:Source" || exit /b 1
call :fetch_dependency "deps\hl2sdk-cs2" "https://github.com/alliedmodders/hl2sdk.git" "7a247626c342c91808daacabb9b5c417dcbab594" "public\iserver.h" "AlliedModders HL2SDK CS2" || exit /b 1
call :fetch_dependency "deps\protobufs" "https://github.com/SteamTracking/Protobufs.git" "7af53a5e1c95852b4394d4789e9e707cc1c8dd35" "csgo\network_connection.proto" "SteamTracking CS2 protobufs" || exit /b 1

powershell -NoProfile -ExecutionPolicy Bypass -File "tools\patch-build-dependencies.ps1" || exit /b 1

echo Configuring a clean MSVC x64 build...
if exist "build" cmake -E remove_directory "build" || exit /b 1
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release ^
  -DCMAKE_C_COMPILER=cl -DCMAKE_CXX_COMPILER=cl ^
  -DMMSOURCE_ROOT="%CD%\deps\metamod-source" ^
  -DHL2SDK_CS2_ROOT="%CD%\deps\hl2sdk-cs2" ^
  -DPROTOBUFS_ROOT="%CD%\deps\protobufs" || exit /b 1
cmake --build build --target luacs_package || exit /b 1

if defined NO_DEPLOY (
  echo LuaCS build and package completed without deployment.
  exit /b 0
)

call deploy.bat "%GAME_ROOT%"
exit /b %ERRORLEVEL%

:fetch_dependency
set "DEP_DIR=%~1"
set "DEP_URL=%~2"
set "DEP_COMMIT=%~3"
set "DEP_MARKER=%~4"
set "DEP_NAME=%~5"
if exist "%DEP_DIR%\%DEP_MARKER%" exit /b 0

echo Fetching %DEP_NAME% at %DEP_COMMIT%...
if exist "%DEP_DIR%" rmdir /S /Q "%DEP_DIR%"
mkdir "%DEP_DIR%" || exit /b 1
git -C "%DEP_DIR%" init || exit /b 1
git -C "%DEP_DIR%" remote add origin "%DEP_URL%" || exit /b 1
git -C "%DEP_DIR%" fetch --depth 1 origin "%DEP_COMMIT%" || exit /b 1
git -C "%DEP_DIR%" checkout --detach FETCH_HEAD || exit /b 1
if not exist "%DEP_DIR%\%DEP_MARKER%" (
  echo ERROR: %DEP_NAME% did not contain %DEP_MARKER%.
  exit /b 1
)
exit /b 0
