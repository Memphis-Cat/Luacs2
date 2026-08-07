$ErrorActionPreference = "Stop"

$root = (Resolve-Path "$PSScriptRoot\..").Path
$traceSource = Join-Path $root "src\modules\traces\traces.cpp"
$traceVerified = Join-Path $root "src\modules\traces\traces_verified.cpp"
$grenadeSource = Join-Path $root "src\modules\grenades\grenades.cpp"
$grenadeVerified = Join-Path $root "src\modules\grenades\grenades_verified.cpp"
$guardSource = Join-Path $root "src\plugin\game_api_advanced_runtime_guards.cpp"
$schemaGrenadeSource = Join-Path $root "src\plugin\game_api_advanced_schema_grenades_build.cpp"
$cmake = Join-Path $root "CMakeLists.txt"
$traceDocs = Join-Path $root "docs\traces.md"
$grenadeDocs = Join-Path $root "docs\grenades.md"
$abiHeader = Join-Path $root "include\luacs\advanced_world_api.h"

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

function Normalize-Whitespace {
    param([Parameter(Mandatory = $true)][string]$Text)
    return [regex]::Replace($Text, '\s+', ' ').Trim()
}

foreach ($requiredFile in @(
    $traceSource,
    $traceVerified,
    $grenadeSource,
    $grenadeVerified,
    $guardSource,
    $schemaGrenadeSource,
    $cmake,
    $traceDocs,
    $grenadeDocs,
    $abiHeader
)) {
    if (-not (Test-Path -LiteralPath $requiredFile -PathType Leaf)) {
        throw "required trace/grenade test file is missing: $requiredFile"
    }
}

$traceText = [System.IO.File]::ReadAllText($traceSource)
Assert-Contains $traceText @(
    'inline constexpr const char* kTraceResultMeta = "LuaCS.TraceResult";',
    'void push_u64_exact(',
    'std::uint64_t read_u64_exact(',
    'hexadecimal ? 16 : 10',
    'luaL_checktype(state, -1, LUA_TBOOLEAN);',
    'trace vector components must be finite',
    'if (value == "line" || value == "ray")',
    'if (value == "hull" || value == "box")',
    'read_optional_vector(state, table, "center", request.center_a);',
    'add_function(state, api, "segment", &line);',
    'add_function(state, api, "direction", &direction);',
    'add_function(state, api, "from_direction", &direction);',
    'add_function(state, api, "box", &hull);',
    'add_function(state, api, "did_hit", &result_did_hit);',
    'lua_setfield(state, -2, "total_distance");',
    'lua_setfield(state, -2, "remaining_distance");',
    'lua_setfield(state, -2, "hit_entity");',
    'lua_setfield(state, -2, "hit_world");',
    'lua_setfield(state, -2, "shape_name");',
    'add_mask(state, "MASK_ALL", 0xFFFFFFFFFFFFFFFFull);'
) "trace module"
Assert-NotContains $traceText @(
    'lua_pushnumber(state, static_cast<lua_Number>(value))',
    'static_cast<std::uint64_t>(luaL_checkinteger(state, -1))'
) "exact trace 64-bit handling"

$traceVerifiedText = [System.IO.File]::ReadAllText($traceVerified)
Assert-Contains $traceVerifiedText @(
    'verified_trace_result_tostring',
    'std::snprintf(buffer, sizeof(buffer),',
    'add_function(state, api, "did_hit_world", &result_hit_world);',
    'add_function(state, api, "did_hit_entity", &result_hit_entity);',
    'lua_setfield(state, -2, "shapes");',
    'lua_setfield(state, -2, "objects");',
    'OBJECTS_ALL_GAME_ENTITIES',
    'MAX_IGNORE_ENTITIES',
    'MAX_MESH_VERTICES'
) "verified trace module"

$grenadeText = [System.IO.File]::ReadAllText($grenadeSource)
Assert-Contains $grenadeText @(
    'inline constexpr const char* kGrenadeMeta = "LuaCS.Grenade";',
    'grenade vector components must be finite',
    'GrenadeType parse_type(',
    'name == "hegrenade"',
    'name == "incgrenade"',
    'thrower and thrower_slot identify different players',
    'luaL_checktype(state, -1, LUA_TBOOLEAN);',
    'void apply_grenade(',
    'int refresh(lua_State* state)',
    'apply_grenade(state, 1, value);',
    'add_function(state, api, "is_valid", &is_valid);',
    'add_function(state, api, "count", &count);',
    'add_function(state, api, "by_owner", &filter_by_owner);',
    'add_function(state, api, "by_thrower", &filter_by_thrower);',
    'add_function(state, api, "create", &spawn);',
    'add_function(state, api, "spawn_he", &spawn_he);',
    'add_function(state, api, "spawn_flashbang", &spawn_flashbang);',
    'add_function(state, api, "spawn_smoke", &spawn_smoke);',
    'add_function(state, api, "spawn_molotov", &spawn_molotov);',
    'add_function(state, api, "spawn_incendiary", &spawn_incendiary);',
    'add_function(state, api, "spawn_decoy", &spawn_decoy);',
    'add_function(state, api, "spawn_inferno", &spawn_inferno);',
    'add_function(state, api, "type_name", &type_name_fn);',
    'add_function(state, api, "type_id", &type_id_fn);',
    'add_function(state, api, "fuse_duration", &grenade_fuse_duration);',
    'lua_setfield(state, table, "projectile");',
    'lua_setfield(state, table, "effect");',
    'lua_setfield(state, -2, "types");'
) "grenade module"

$grenadeVerifiedText = [System.IO.File]::ReadAllText($grenadeVerified)
Assert-Contains $grenadeVerifiedText @(
    '#include <cstdio>',
    '#include "grenades.cpp"',
    'Do not rely on Lua headers to transitively'
) "verified grenade module translation unit"

$guardText = [System.IO.File]::ReadAllText($guardSource)
Assert-Contains $guardText @(
    '#include "game_api_advanced_schema_grenades_build.cpp"',
    'bool validate_trace_output(',
    'Source 2 returned an invalid trace fraction',
    'Source 2 trace miss unexpectedly retained a hit entity',
    'bool validate_grenade_output(',
    'Source 2 returned a grenade entity that is no longer valid',
    'Source 2 returned non-finite grenade timing data',
    'g_guarded_trace_base = services.trace;',
    'g_guarded_grenade_get_base = services.grenade_get;',
    'g_guarded_grenade_at_base = services.grenade_at;',
    'g_guarded_grenade_spawn_base = services.grenade_spawn;',
    'services.trace = &trace_runtime_guard;',
    'services.grenade_get = &grenade_get_runtime_guard;',
    'services.grenade_at = &grenade_at_runtime_guard;',
    'services.grenade_spawn = &grenade_spawn_runtime_guard;'
) "native trace/grenade guard"

$schemaText = [System.IO.File]::ReadAllText($schemaGrenadeSource)
Assert-Contains $schemaText @(
    'initialize_projectile_schema(',
    'schedule_schema_fuse(',
    'm_vInitialPosition',
    'm_vInitialVelocity',
    'm_vecOriginalSpawnLocation',
    'm_bDetonationRecorded',
    'm_hOriginalThrower',
    'm_flDetonateTime',
    'grenade_detonate_schema'
) "schema-native grenade layer"

$cmakeText = [System.IO.File]::ReadAllText($cmake)
Assert-Contains $cmakeText @(
    'game_api_advanced_runtime_guards.cpp',
    '"${LUACS_ADVANCED_GENERATED_DIR}/game_api_advanced_runtime_guards.cpp"',
    'add_luacs_module(traces src/modules/traces/traces_verified.cpp)',
    'add_luacs_module(grenades src/modules/grenades/grenades_verified.cpp)',
    'add_compile_options(/utf-8 /MP /WX)'
) "CMake trace/grenade selection"

$abiText = [System.IO.File]::ReadAllText($abiHeader)
Assert-Contains $abiText @(
    'kAdvancedWorldServicesAbiVersion = 3;',
    'kTraceIgnoreCapacity = 64;',
    'kTraceMeshVertexCapacity = 256;',
    'enum class TraceShape',
    'enum class GrenadeType'
) "AdvancedWorld ABI"

$traceDocsText = Normalize-Whitespace ([System.IO.File]::ReadAllText($traceDocs))
Assert-Contains $traceDocsText @(
    '# LuaCS traces',
    '## Exact 64-bit masks and pointers',
    '## Trace results',
    '## Native validation boundary',
    'MASK_ALL` is represented exactly rather than wrapping to',
    'result:did_hit_world()',
    'result:did_hit_entity(entity_or_player)',
    '`hit_entity` and `hit_world` are boolean fields.'
) "trace documentation"

$grenadeDocsText = Normalize-Whitespace ([System.IO.File]::ReadAllText($grenadeDocs))
Assert-Contains $grenadeDocsText @(
    '# LuaCS grenades',
    '## Supported types',
    '## Grenade objects',
    '## Native validation boundary',
    '`refresh()` now updates the same Lua object in place',
    'spawn_incendiary',
    'by_thrower(player_or_slot'
) "grenade documentation"

Write-Host (
    "LuaCS trace/grenade tests passed: exact unsigned 64-bit trace values, " +
    "strict finite/filter inputs, all five Source 2 shapes, unambiguous result " +
    "helpers, schema-native transactional grenade creation, mutable grenade " +
    "objects, type/filter/spawn helpers, final native output guards, explicit " +
    "module dependencies, and AdvancedWorld ABI v3 compatibility are all " +
    "represented in source.")
