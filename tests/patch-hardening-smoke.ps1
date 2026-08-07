$ErrorActionPreference = "Stop"

$root = (Resolve-Path "$PSScriptRoot\..").Path

function Read-Required {
    param([Parameter(Mandatory = $true)][string]$RelativePath)
    $path = Join-Path $root $RelativePath
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "required patch-hardening file is missing: $RelativePath"
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
    'add_luacs_module(traces src/modules/traces/traces_verified.cpp)',
    'add_luacs_module(grenades src/modules/grenades/grenades_verified.cpp)',
    'game_api_advanced_runtime_guards.cpp'
) "CMake warning policy and verified layers"

$generatedInjection = Read-Required "tools\inject-generated-game-api.cmake"
Assert-Contains $generatedInjection @(
    'sourcehook_impl_chookmaninfo.cpp',
    '${sourcehook_hookman_source}',
    'target_sources(luacs2 PRIVATE'
) "complete SourceHook generated-source injection"

$smg = Read-Required "src\common\smg.cpp"
Assert-Contains $smg @(
    'MoveFileExW(',
    'MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH',
    'BCryptHashData(',
    'BCryptFinishHash(',
    'temporary',
    'load_or_create_key'
) "SMG atomic/CNG implementation"

$compiler = Read-Required "src\compiler\main.cpp"
Assert-Contains $compiler @(
    'valid_utf8(',
    'source.find(',
    'unknown LuaCS module',
    'lua_extension(',
    'failure_count',
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
    'std::from_chars(',
    'checked_table_capacity('
) "shared checked Lua conversions"

$entities = Read-Required "src\modules\entities\entities.cpp"
Assert-Contains $entities @(
    'rollback_created(',
    'valid_entity_info(',
    'entity vector components must be finite',
    'return teleport_impl(state, nullptr, nullptr, &velocity);'
) "entity hardening"
Assert-NotContains $entities @(
    'lua_replace(state, 4)',
    'static_cast<int>(luaL_checkinteger'
) "entity stack/narrowing regressions"

$weapons = Read-Required "src\modules\weapons\weapons.cpp"
Assert-Contains $weapons @(
    'valid_weapon_info(',
    'checked_class_name(',
    'checked_table_capacity(',
    'optional_boolean(state, 4, true)',
    'replacement weapon could not be equipped'
) "weapon hardening"

$events = Read-Required "src\modules\events\events.cpp"
Assert-Contains $events @(
    'push_u64_exact(state, id)',
    'read_u64_exact(state, 1, "event subscription id")',
    'read_u64_exact(state, 3, "event uint64")',
    'strict_boolean(state, 3)',
    'event float must be finite'
) "event exact-value/type hardening"

$sounds = Read-Required "src\modules\sounds\sounds.cpp"
Assert-Contains $sounds @(
    'push_u64_exact(state, sound.recipients_mask)',
    'read_u64_exact(',
    'valid_sound(',
    'volume must be finite and between 0 and 10',
    'recipient filter contains no connected players'
) "sound exact-mask hardening"

$teams = Read-Required "src\modules\teams\teams.cpp"
Assert-Contains $teams @(
    'const std::int64_t result =',
    'resulting score must be between 0 and INT_MAX'
) "team score overflow hardening"

$runtime = Read-Required "src\runtime\runtime.cpp"
Assert-Contains $runtime @(
    'std::filesystem::directory_iterator',
    'std::error_code',
    'truncate_utf8(',
    'authenticated SMG package contains empty Lua bytecode',
    'Repeating timer overflowed its due time'
) "runtime filesystem/timer hardening"

$runtimeServices = Read-Required "src\runtime\runtime_services.cpp"
Assert-Contains $runtimeServices @(
    'call_with_error(',
    'luaL_unref(state, LUA_REGISTRYINDEX, reference);',
    'next_event_subscription_id_',
    'next_timer_id_',
    'Invalid player slot or non-finite teleport vector.',
    'Weapon inventory service threw an unknown exception.'
) "runtime service boundary hardening"

$runtimeLua = Read-Required "src\runtime\runtime_lua.cpp"
Assert-Contains $runtimeLua @(
    'LoadLibraryW(native_path.c_str())',
    'FreeLibrary(handle);',
    'could not retain module',
    'native module threw C++ exception',
    'push_u64_exact(state, player.steam64)',
    'Vector x component must be finite'
) "runtime Lua/module-loader hardening"

$runtimeEvents = Read-Required "src\runtime\runtime_events.cpp"
Assert-Contains $runtimeEvents @(
    'Ignored player_connect with invalid slot',
    'push_u64_exact(vm.state, token)',
    'Could not snapshot event callbacks',
    'Could not snapshot command callbacks',
    'Could not enumerate plugin packages',
    'Server-command callback threw',
    'disable_vm(vm, context);'
) "runtime event/admin/quarantine hardening"

$pluginHeader = Read-Required "src\plugin\plugin.h"
Assert-Contains $pluginHeader @(
    'kEventCopyCapacity = 1024',
    'std::array<IGameEvent*, kEventCopyCapacity>',
    'event_copy_overflow_depth_'
) "bounded event-copy bookkeeping"

$plugin = Read-Required "src\plugin\plugin.cpp"
Assert-Contains $plugin @(
    'release_lua_dependency()',
    'LoadLibraryExW(',
    'GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT',
    'kConsoleChunk = 4096',
    'Game-event nesting exceeded the bounded',
    'runtime_.load_plugins();'
) "Metamod startup/event hardening"
Assert-NotContains $plugin @(
    'static_cast<int>(line.size())'
) "console length narrowing"

$serverModule = Read-Required "src\plugin\server_module.cpp"
Assert-Contains $serverModule @(
    'GetModuleHandleExW(',
    'GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS',
    'GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT',
    'GetModuleFileNameW(',
    '_wcsicmp(resolved_path.filename().c_str(), L"server.dll")',
    'L"\\addons\\metamod\\"',
    'g_game_server_module = module',
    'g_game_server_module_path = resolved_path'
) "server module binding hardening"

$world = Read-Required "src\plugin\game_api_world_build.cpp"
Assert-Contains $world @(
    'valid_entity_info_checked(',
    'entity_teleport_checked(',
    'round_restart_checked(',
    'sound_emit_checked(',
    'services.entity_get = &entity_get_checked;',
    'services.sound_emit = &sound_emit_checked;'
) "WorldServices validation boundary"

$signatureGenerator = Read-Required "tools\generate-disk-backed-game-api.ps1"
Assert-Contains $signatureGenerator @(
    'VirtualQuery(',
    'readable_memory(',
    'PAGE_GUARD | PAGE_NOACCESS',
    'could not safely apply',
    'disk-backed LuaCS gamedata'
) "generated signature dereference hardening"

$pluginGenerator = Read-Required "tools\generate-server-module-plugin.ps1"
Assert-Contains $pluginGenerator @(
    'Assert-ExactlyOnce $text $initializationMarker "game API initialization"',
    'release_lua_dependency();',
    'path_text(LuaCSGameServerModulePath())',
    'Could not reset the current-session native error log',
    'command_name == "say" || command_name == "say_team"',
    'player_slot >= 0 && player_slot < 64'
) "generated plugin hardening"
Assert-NotContains $pluginGenerator @(
    'game_api_.initialize(root, LuaCSGameServerModulePath(), game_api_error)'
) "generated plugin nonexistent overload regression"

$build = Read-Required "build.bat"
Assert-Contains $build @(
    'git status --porcelain --untracked-files^=no',
    'Refusing to stamp a build from modified or staged tracked files.',
    'git -C "%DEP_DIR%" reset --hard',
    'git -C "%DEP_DIR%" clean -fdx',
    'instead of %DEP_COMMIT%'
) "deterministic build/stamp hardening"

$generatedGameApi = Join-Path $root "build\generated\plugin\game_api.cpp"
if (Test-Path -LiteralPath $generatedGameApi -PathType Leaf) {
    $generated = [System.IO.File]::ReadAllText($generatedGameApi)
    Assert-Contains $generated @(
        'bool readable_memory(',
        'VirtualQuery(',
        'if (!readable_memory(address, sizeof(T))) return false;',
        'disk-backed LuaCS gamedata'
    ) "generated game API scanner"
    Assert-NotContains $generated @(
        'if (!address) return false;`n    std::memcpy(&value'
    ) "unguarded generated pointer reads"
}

Write-Host (
    "LuaCS patch hardening tests passed: /WX policy, atomic SMG/key writes, " +
    "compiler preflight diagnostics, checked Lua conversions, exact uint64 " +
    "event/sound values, overflow-safe teams, entity lifecycle/stack safety, " +
    "runtime exception/quarantine and filesystem boundaries, native DLL " +
    "cleanup, complete SourceHook selection, bounded game-event copies, " +
    "validated WorldServices outputs, deterministic build stamps, and guarded " +
    "generated signature dereferences are represented in source.")
