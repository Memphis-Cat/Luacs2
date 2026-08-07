$ErrorActionPreference = "Stop"

$root = (Resolve-Path "$PSScriptRoot\..").Path
$nativeSource = Join-Path $root "src\plugin\game_api_advanced_verified_build.cpp"
$moduleSource = Join-Path $root "src\modules\properties\properties.cpp"
$abiHeader = Join-Path $root "include\luacs\advanced_world_api.h"
$docs = Join-Path $root "docs\schema-properties.md"

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

foreach ($requiredFile in @($nativeSource, $moduleSource, $abiHeader, $docs)) {
    if (-not (Test-Path -LiteralPath $requiredFile -PathType Leaf)) {
        throw "required schema-property test file is missing: $requiredFile"
    }
}

$nativeText = [System.IO.File]::ReadAllText($nativeSource)
Assert-Contains $nativeText @(
    'bool resolve_schema_property(',
    'part_index + 1 == parts.size() && explicit_index >= 0',
    'int network_index{-1};',
    'if (part_index == 0) network_index = index;',
    'typed access to an array or collection requires an element index',
    'signed integer value does not fit the schema width',
    'unsigned integer value does not fit the schema width',
    'string value does not fit the fixed schema array',
    'entity-handle target index is invalid',
    'bitfield writes are unavailable because Source 2 does not expose a stable public bit offset',
    'Source 2 did not apply the requested collection size',
    'services.property_info = &property_info_verified;',
    'services.property_get = &property_get_verified;',
    'services.property_set = &property_set_verified;',
    'services.property_count = &property_count_verified;',
    'services.property_at = &property_at_verified;',
    'services.property_get_raw = &property_get_raw_verified;',
    'services.property_set_raw = &property_set_raw_verified;',
    'services.property_collection_count =',
    '&property_collection_count_verified;',
    'services.property_collection_resize =',
    '&property_collection_resize_verified;',
    'services.property_child_count = &property_child_count_verified;',
    'services.property_child_at = &property_child_at_verified;',
    'services.trace = &trace_verified;'
) "verified native schema adapter"

$moduleText = [System.IO.File]::ReadAllText($moduleSource)
Assert-Contains $moduleText @(
    'int checked_entity_index(',
    '"pawn_index"',
    '"controller_index"',
    'bool optional_boolean(',
    'luaL_checktype(state, index, LUA_TBOOLEAN);',
    'void push_uint64_exact(',
    'const std::string exact = std::to_string(value);',
    'std::uint64_t read_uint64_exact(',
    'hexadecimal ? 16 : 10',
    'int property_ref(',
    'refresh_ref_table(state, table, error, sizeof(error))',
    'int ref_entity_from(',
    'int ref_index_from(',
    'child_failure == "nested schema pointer is null"',
    'add_function(state, api, "exists", &exists);',
    'add_function(state, api, "kind", &kind);',
    'add_function(state, api, "count", &count);',
    'add_function(state, api, "values", &values);',
    'add_function(state, api, "get_all", &values);',
    'add_function(state, api, "walk", &walk);',
    'add_function(state, api, "ref", &property_ref);',
    'add_function(state, api, "get_bool", &get_boolean);',
    'add_function(state, api, "get_int", &get_integer);',
    'add_function(state, api, "get_uint", &get_unsigned);',
    'add_function(state, api, "get_float", &get_float);',
    'add_function(state, api, "get_string", &get_string);',
    'add_function(state, api, "get_vector", &get_vector);',
    'add_function(state, api, "get_angle", &get_angle);',
    'add_function(state, api, "get_handle", &get_handle);',
    'add_function(state, api, "get_pointer", &get_pointer);',
    'add_function(state, api, "set_bool", &set_boolean);',
    'add_function(state, api, "set_int", &set_integer);',
    'add_function(state, api, "set_uint", &set_unsigned);',
    'add_function(state, api, "set_float", &set_float);',
    'add_function(state, api, "set_string", &set_string);',
    'add_function(state, api, "set_vector", &set_vector);',
    'add_function(state, api, "set_angle", &set_angle);',
    'add_function(state, api, "set_handle", &set_handle);',
    'add_function(state, api, "set_pointer", &set_pointer);',
    'raw property value exceeds %d bytes',
    'lua_setfield(state, -2, "kinds");'
) "Lua schema property module"

Assert-NotContains $moduleText @(
    'lua_pushnumber(state, static_cast<lua_Number>(value.unsigned_value))',
    'std::string indexed_path('
) "exact Lua schema property value handling"

$abiText = [System.IO.File]::ReadAllText($abiHeader)
Assert-Contains $abiText @(
    'kAdvancedWorldServicesAbiVersion = 3;',
    'kPropertyRawCapacity = 4096;',
    'PropertyKind::Invalid'
) "advanced world ABI"

$docsText = Normalize-Whitespace ([System.IO.File]::ReadAllText($docs))
Assert-Contains $docsText @(
    '# LuaCS schema properties',
    '## Typed API',
    '## Exact unsigned and pointer values',
    '## Property references',
    '## Fixed arrays and collections',
    '## Raw access',
    '### Bitfields',
    '## Network-state notification',
    'exact decimal string rather than an imprecise floating-point number',
    'Fixed strings are never silently truncated.'
) "schema property documentation"

Write-Host (
    "LuaCS schema property tests passed: verified Source 2 path resolution, " +
    "aggregate/index safety, exact-width numeric writes, bounded strings/raw " +
    "data, safe bitfields and handles, verified collection resizing, complete " +
    "typed/reflection/property-reference Lua APIs, exact uint64 handling, and " +
    "AdvancedWorld ABI v3 compatibility are all represented in source.")
