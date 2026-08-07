$ErrorActionPreference = "Stop"

$root = (Resolve-Path "$PSScriptRoot\..").Path

function Read-Required {
    param([Parameter(Mandatory = $true)][string]$RelativePath)
    $path = Join-Path $root $RelativePath
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "required hardening-contract file is missing: $RelativePath"
    }
    return [System.IO.File]::ReadAllText($path)
}

function Assert-Contains {
    param(
        [Parameter(Mandatory = $true)][string]$Text,
        [Parameter(Mandatory = $true)][string[]]$Tokens,
        [Parameter(Mandatory = $true)][string]$Context
    )
    foreach ($token in $Tokens) {
        if (-not $Text.Contains($token)) {
            throw "$Context is missing '$token'"
        }
    }
}

function Assert-NotContains {
    param(
        [Parameter(Mandatory = $true)][string]$Text,
        [Parameter(Mandatory = $true)][string[]]$Tokens,
        [Parameter(Mandatory = $true)][string]$Context
    )
    foreach ($token in $Tokens) {
        if ($Text.Contains($token)) {
            throw "$Context unexpectedly contains '$token'"
        }
    }
}

$cmake = Read-Required "CMakeLists.txt"
Assert-Contains $cmake @(
    'add_compile_options(/utf-8 /MP /WX)',
    'set_source_files_properties(${LUA_SOURCES} PROPERTIES LANGUAGE CXX)',
    'sourcehook_impl_chookmaninfo.cpp',
    'add_luacs_module(traces src/modules/traces/traces_verified.cpp)',
    'add_luacs_module(grenades src/modules/grenades/grenades_verified.cpp)',
    'game_api_advanced_runtime_guards.cpp'
) "CMake hardening contract"

$injection = Read-Required "tools\inject-generated-game-api.cmake"
Assert-Contains $injection @(
    'set(found_sourcehook_hookman FALSE)',
    'set(found_sourcehook_hookman TRUE)',
    'if(NOT found_sourcehook_hookman)',
    'lua_c_api_linkage.h',
    'target_sources(lua55 PRIVATE "${lua_c_api_linkage_header}")',
    'target_sources(lua55_static PRIVATE "${lua_c_api_linkage_header}")',
    'target_compile_options(lua55 PRIVATE "/FI${lua_c_api_linkage_header}")',
    'target_compile_options(lua55_static PRIVATE "/FI${lua_c_api_linkage_header}")'
) "generated-source/Lua ABI injection"
Assert-NotContains $injection @(
    '${sourcehook_hookman_source}'
) "SourceHook duplicate-source regression"

$linkage = Read-Required "src\common\lua_c_api_linkage.h"
Assert-Contains $linkage @(
    'extern "C" {',
    '#include "lua.h"',
    '#include "lauxlib.h"',
    '#include "lualib.h"',
    'LUACS_TEMP_LUA_CORE',
    'LUACS_TEMP_LUA_LIB'
) "Lua C ABI preservation"

$smg = Read-Required "src\common\smg.cpp"
Assert-Contains $smg @(
    'MoveFileExW(',
    'MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH',
    'BCryptHashData(',
    'BCryptFinishHash(',
    'load_or_create_key'
) "SMG atomic/authenticated writes"

$compiler = Read-Required "src\compiler\main.cpp"
Assert-Contains $compiler @(
    'valid_utf8(',
    'validate_literal_requires(',
    'unknown LuaCS module',
    'preflight_failures',
    'ALREADY COMPILED',
    'find_warnings('
) "compiler hardening"

$checked = Read-Required "include\luacs\lua_checked.h"
Assert-Contains $checked @(
    'checked_int(',
    'checked_slot(',
    'strict_boolean(',
    'finite_number(',
    'finite_float(',
    'push_u64_exact(',
    'read_u64_exact(',
    'std::from_chars('
) "shared Lua boundary checks"

$entities = Read-Required "src\modules\entities\entities.cpp"
Assert-Contains $entities @(
    'rollback_created(',
    'valid_entity_info(',
    'entity vector components must be finite',
    'return teleport_impl(state, nullptr, nullptr, &velocity);'
) "entity lifecycle/stack hardening"
Assert-NotContains $entities @(
    'lua_replace(state, 4)',
    'static_cast<int>(luaL_checkinteger'
) "entity narrowing/stack regressions"

$runtime = Read-Required "src\runtime\runtime.cpp"
Assert-Contains $runtime @(
    'authenticated SMG package contains empty Lua bytecode',
    'Repeating timer overflowed its due time',
    'log(vm, "[ERROR] Repeating timer overflowed its due time and was cancelled.");'
) "runtime timer/package hardening"
Assert-NotContains $runtime @(
    'log(*vm, "[ERROR] Repeating timer overflowed its due time and was cancelled.");'
) "ScriptVm reference regression"

$runtimeLua = Read-Required "src\runtime\runtime_lua.cpp"
Assert-Contains $runtimeLua @(
    'LoadLibraryW(native_path.c_str())',
    'FreeLibrary(handle);',
    'native module threw C++ exception',
    'push_u64_exact(state, player.steam64)',
    'if (vm_disabled(vm.state))',
    'disable_vm(vm.state',
    'Plugin disabled after an uncaught Lua callback error'
) "runtime module/quarantine hardening"

$plugin = Read-Required "src\plugin\plugin.cpp"
Assert-Contains $plugin @(
    'release_lua_dependency()',
    'LoadLibraryExW(',
    'kConsoleChunk = 4096',
    'Game-event nesting exceeded the bounded',
    'std::min<std::size_t>(line.size(), 1u << 20)));'
) "Metamod/native logger hardening"
Assert-NotContains $plugin @(
    'std::min<std::size_t>(line.size(), 1u << 20))));',
    'static_cast<int>(line.size())'
) "native logger/console regressions"

$pluginHeader = Read-Required "src\plugin\plugin.h"
Assert-Contains $pluginHeader @(
    'kEventCopyCapacity = 1024',
    'std::array<IGameEvent*, kEventCopyCapacity>',
    'event_copy_overflow_depth_'
) "bounded event-copy storage"

$serverModule = Read-Required "src\plugin\server_module.cpp"
Assert-Contains $serverModule @(
    'GetModuleHandleExW(',
    'GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS',
    'GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT',
    'GetModuleFileNameW(',
    '_wcsicmp(resolved_path.filename().c_str(), L"server.dll")',
    'g_game_server_module = module'
) "actual server.dll binding"

$world = Read-Required "src\plugin\game_api_world_build.cpp"
Assert-Contains $world @(
    'valid_entity_info_checked(',
    'entity_teleport_checked(',
    'round_restart_checked(',
    'sound_emit_checked(',
    'services.entity_get = &entity_get_checked;',
    'services.sound_emit = &sound_emit_checked;'
) "WorldServices output validation"

$signatureGenerator = Read-Required "tools\generate-disk-backed-game-api.ps1"
Assert-Contains $signatureGenerator @(
    'VirtualQuery(',
    'readable_memory(',
    'PAGE_GUARD | PAGE_NOACCESS',
    'could not safely apply'
) "signature dereference safety"

$pluginGenerator = Read-Required "tools\generate-server-module-plugin.ps1"
Assert-Contains $pluginGenerator @(
    'function Normalize-Newlines',
    'Assert-ExactlyOnce $text $initializationMarker "game API initialization"',
    '#include <cctype>',
    '#include <cstring>',
    'copy->GetString("text", "")',
    'copy->GetPlayerSlot("userid").Get()',
    'command_name == "say" || command_name == "say_team"'
) "generated plugin contract"
Assert-NotContains $pluginGenerator @(
    'game_api_.initialize(root, LuaCSGameServerModulePath(), game_api_error)',
    'game_api_.event_player_slot(',
    'game_api_.event_string('
) "generated plugin API drift"

Write-Host (
    "LuaCS hardening contract tests passed: /WX, C++ Lua unwind with C ABI, " +
    "atomic SMG writes, checked Lua conversions, entity/timer safety, plugin " +
    "quarantine, bounded event copies, actual server module binding, guarded " +
    "signature reads, validated WorldServices, and strict generated-source " +
    "contracts are represented in current source.")

exit 0
