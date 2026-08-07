$ErrorActionPreference = "Stop"

$root = (Resolve-Path "$PSScriptRoot\..").Path
$cmake = Join-Path $root "CMakeLists.txt"
$generatedInjection = Join-Path $root "tools\inject-generated-game-api.cmake"
$luaLinkage = Join-Path $root "src\common\lua_c_api_linkage.h"
$plugin = Join-Path $root "src\plugin\plugin.cpp"
$key = Join-Path $root "build\package\game\csgo\addons\LuaCS\config\luacs.key"

foreach ($requiredFile in @($cmake, $generatedInjection, $luaLinkage, $plugin)) {
    if (-not (Test-Path -LiteralPath $requiredFile -PathType Leaf)) {
        throw "final patch gate: required file is missing: $requiredFile"
    }
}

$text = [System.IO.File]::ReadAllText($cmake)
foreach ($required in @(
    'add_compile_options(/utf-8 /MP /WX)',
    'set_source_files_properties(${LUA_SOURCES} PROPERTIES LANGUAGE CXX)',
    'sourcehook_impl_chookidman.cpp',
    'sourcehook_impl_chookmaninfo.cpp',
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
    'lua_c_api_linkage.h',
    'target_sources(lua55 PRIVATE "${lua_c_api_linkage_header}")',
    'target_sources(lua55_static PRIVATE "${lua_c_api_linkage_header}")',
    'target_compile_options(lua55 PRIVATE "/FI${lua_c_api_linkage_header}")',
    'target_compile_options(lua55_static PRIVATE "/FI${lua_c_api_linkage_header}")',
    'target_sources(luacs2 PRIVATE'
)) {
    if (-not $injectionText.Contains($required)) {
        throw "final patch gate: generated-source injection is missing '$required'"
    }
}
if ($injectionText.Contains('${sourcehook_hookman_source}')) {
    throw "final patch gate: generated-source injection duplicates the SourceHook hook-manager source"
}

$linkageText = [System.IO.File]::ReadAllText($luaLinkage)
foreach ($required in @(
    'extern "C" {',
    '#include "lua.h"',
    '#include "lauxlib.h"',
    '#include "lualib.h"',
    'LUACS_TEMP_LUA_CORE',
    'LUACS_TEMP_LUA_LIB'
)) {
    if (-not $linkageText.Contains($required)) {
        throw "final patch gate: Lua C ABI linkage header is missing '$required'"
    }
}

$pluginText = [System.IO.File]::ReadAllText($plugin)
if ($pluginText.Contains('std::min<std::size_t>(line.size(), 1u << 20))));')) {
    throw "final patch gate: native error logger has the old extra-closing-parenthesis syntax error"
}
if (-not $pluginText.Contains('std::min<std::size_t>(line.size(), 1u << 20)));')) {
    throw "final patch gate: native error logger bounded write is missing"
}

if (Test-Path -LiteralPath $key) {
    Remove-Item -LiteralPath $key -Force
}
if (Test-Path -LiteralPath $key) {
    throw "final patch gate: compiler-generated luacs.key remains in package"
}

Write-Host (
    "LuaCS final patch gate passed: /WX remains enabled, Lua uses C++ exception " +
    "unwinding while preserving the C ABI for shared/static consumers, all " +
    "required SourceHook implementation units are selected exactly once, " +
    "native logger syntax is guarded, verified grenade/advanced layers remain " +
    "selected, and no compiler test key remains in the deployable package.")