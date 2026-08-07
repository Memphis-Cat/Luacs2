$ErrorActionPreference = "Stop"

$root = (Resolve-Path "$PSScriptRoot\..").Path
$cmake = Join-Path $root "CMakeLists.txt"
$generatedInjection = Join-Path $root "tools\inject-generated-game-api.cmake"
$key = Join-Path $root "build\package\game\csgo\addons\LuaCS\config\luacs.key"

if (-not (Test-Path -LiteralPath $cmake -PathType Leaf)) {
    throw "CMakeLists.txt is missing"
}
if (-not (Test-Path -LiteralPath $generatedInjection -PathType Leaf)) {
    throw "generated-source CMake injection is missing"
}

$text = [System.IO.File]::ReadAllText($cmake)
foreach ($required in @(
    'add_compile_options(/utf-8 /MP /WX)',
    'set_source_files_properties(${LUA_SOURCES} PROPERTIES LANGUAGE CXX)',
    'sourcehook_impl_chookidman.cpp',
    'sourcehook_impl_cvfnptr.cpp',
    'sourcehook_impl_cproto.cpp',
    'add_luacs_module(grenades src/modules/grenades/grenades_verified.cpp)',
    'game_api_advanced_runtime_guards.cpp'
)) {
    if (-not $text.Contains($required)) {
        throw "final patch gate: CMake is missing '$required'"
    }
}

$injectionText = [System.IO.File]::ReadAllText($generatedInjection)
foreach ($required in @(
    'sourcehook_impl_chookmaninfo.cpp',
    'target_sources(luacs2 PRIVATE',
    '${sourcehook_hookman_source}'
)) {
    if (-not $injectionText.Contains($required)) {
        throw "final patch gate: generated-source injection is missing '$required'"
    }
}

if (Test-Path -LiteralPath $key) {
    Remove-Item -LiteralPath $key -Force
}
if (Test-Path -LiteralPath $key) {
    throw "final patch gate: compiler-generated luacs.key remains in package"
}

Write-Host (
    "LuaCS final patch gate passed: /WX remains enabled, Lua uses C++ exception " +
    "unwinding, all required SourceHook implementation units are selected, " +
    "verified grenade/advanced layers remain selected, and no compiler test " +
    "key remains in the deployable package.")
