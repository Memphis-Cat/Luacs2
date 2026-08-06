$ErrorActionPreference = "Stop"

$root = Resolve-Path "$PSScriptRoot\.."
$addons = Join-Path $root "build\package\game\csgo\addons"
$luaCs = Join-Path $addons "LuaCS"
$bin = Join-Path $luaCs "bin\win64"
$compiler = Join-Path $luaCs "scripting\compile.exe"
$source = Join-Path $luaCs "scripting\example_welcome.lua"
$output = Join-Path $luaCs "plugins\example_welcome.smg"
$key = Join-Path $luaCs "config\luacs.key"
$badSource = Join-Path $luaCs "scripting\syntax_failure.lua"
$badOutput = Join-Path $luaCs "plugins\syntax_failure.smg"
$vdf = Join-Path $addons "metamod\luacs2.vdf"
$advancedGamedata = Join-Path $luaCs "gamedata\reference\advanced_windows_gamedata.json"

function Assert-SourceTokens {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string[]]$Tokens
    )
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "required source file is missing: $Path"
    }
    $text = Get-Content -LiteralPath $Path -Raw
    foreach ($token in $Tokens) {
        if ($text -notmatch [regex]::Escape($token)) {
            throw "source file '$Path' is missing '$token'"
        }
    }
}

function Assert-SourceOmits {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string[]]$Tokens
    )
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "required source file is missing: $Path"
    }
    $text = Get-Content -LiteralPath $Path -Raw
    foreach ($token in $Tokens) {
        if ($text -match [regex]::Escape($token)) {
            throw "source file '$Path' still contains obsolete token '$token'"
        }
    }
}

try {
    foreach ($name in @(
        "luacs2.dll", "lua55.dll", "events.dll", "timers.dll",
        "players.dll", "commands.dll", "math.dll", "weapons.dll",
        "hud.dll", "cvars.dll", "teams.dll", "rounds.dll",
        "entities.dll", "sounds.dll", "properties.dll", "traces.dll",
        "grenades.dll"
    )) {
        if (-not (Test-Path -LiteralPath (Join-Path $bin $name) -PathType Leaf)) {
            throw "required native file is missing from bin\win64: $name"
        }
    }

    $cmakeFile = Join-Path $root "CMakeLists.txt"
    $gameApiSource = Join-Path $root "src\plugin\game_api.cpp"
    $pluginSource = Join-Path $root "src\plugin\plugin.cpp"
    $runtimeHeader = Join-Path $root "src\runtime\runtime.h"
    $runtimeSource = Join-Path $root "src\runtime\runtime.cpp"
    $runtimeEvents = Join-Path $root "src\runtime\runtime_events.cpp"
    $exampleSource = Join-Path $root "packaging\LuaCS\scripting\example_welcome.lua"
    $advancedHeader = Join-Path $root "include\luacs\advanced_world_api.h"
    $advancedAdapter = Join-Path $root "src\plugin\game_api_advanced_build.cpp"
    $advancedComplete = Join-Path $root "src\plugin\game_api_advanced_complete_build.cpp"
    $advancedFinal = Join-Path $root "src\plugin\game_api_advanced_final_build.cpp"
    $advancedVerified = Join-Path $root "src\plugin\game_api_advanced_verified_build.cpp"
    $advancedSchemaGrenades = Join-Path $root "src\plugin\game_api_advanced_schema_grenades_build.cpp"
    $generatedAdvanced = Join-Path $root "build\generated\advanced\game_api_advanced.cpp"
    $propertiesSource = Join-Path $root "src\modules\properties\properties.cpp"
    $tracesSource = Join-Path $root "src\modules\traces\traces.cpp"
    $tracesVerified = Join-Path $root "src\modules\traces\traces_verified.cpp"
    $grenadesSource = Join-Path $root "src\modules\grenades\grenades.cpp"

    Assert-SourceTokens $cmakeFile @(
        "LUACS_LEGACY_NATIVE_RAY", "using NativeRay = Ray_t;",
        "CMAKE_CONFIGURE_DEPENDS", "generated/advanced",
        "game_api_advanced_schema_grenades_build.cpp",
        "add_luacs_module(traces src/modules/traces/traces_verified.cpp)",
        "sourcehook_impl_chookmaninfo.cpp"
    )
    Assert-SourceTokens $generatedAdvanced @(
        "using NativeRay = Ray_t;", "using TraceShapeFn"
    )
    Assert-SourceOmits $generatedAdvanced @(
        "std::array<std::byte, 40> data{};",
        "static_assert(sizeof(NativeRay) == 44);",
        "ray.data.data()", "ray.type = 2"
    )

    Assert-SourceTokens $gameApiSource @(
        "missing_functions",
        "could not resolve required CS2 function signature(s):",
        "manager ? *reinterpret_cast<void**>(manager) : nullptr"
    )
    Assert-SourceOmits $gameApiSource @(
        "could not resolve one or more required CS2 functions or the CGameEventManager vtable"
    )
    Assert-SourceTokens $pluginSource @(
        "GET_V_IFACE_CURRENT(GetEngineFactory, g_game_events, IGameEventManager2",
        "ConCommand g_lua_command",
        "META_CONVAR_REGISTER(FCVAR_RELEASE)",
        "g_SMAPI->UnregisterConCommand(g_PLAPI, &g_lua_command)",
        "ConVar_Unregister()",
        "failed_hooks",
        "fire_event_pre_hook_id_ <= 0",
        "client_command_hook_id_ <= 0",
        "Could not install required Source 2 hook(s):",
        "Installed all 8 required Source 2 hooks."
    )
    Assert-SourceOmits $pluginSource @(
        "SH_ADD_DVPHOOK", "META_REGCVAR", "META_UNREGCVAR"
    )

    Assert-SourceTokens $runtimeHeader @(
        'kLuaCSVersion = "0.5.0"', "struct PluginMetadata",
        "plugin_metadata_cache_", "plugin_failures_",
        "read_plugin_metadata"
    )
    Assert-SourceTokens $runtimeSource @(
        "read_plugin_metadata(*vm)", 'lua_getglobal(vm.state, "plugin")',
        'read_field("name", 256)', 'read_field("author", 256)',
        'read_field("version", 128)', 'read_field("description", 1024)',
        "plugin_failures_[key]"
    )
    Assert-SourceTokens $runtimeEvents @(
        'section == "clear"', 'section == "version"',
        'action == "list"', 'action == "info"', 'action == "load"',
        'action == "unload"', 'action == "refresh"',
        'action == "retry"', 'action == "force_load"',
        'action == "force_unload"',
        'lua_getglobal(vm.state, "OnUnload")',
        'lua_getfield(vm.state, plugin_table, "unload")',
        '"source alias: "', '"Last error: "'
    )
    Assert-SourceTokens $exampleSource @(
        "plugin = {", 'name = "Welcome Example"',
        'author = "Memphis-Cat"', 'version = "1.0.0"',
        "description =", "function plugin:unload()"
    )

    Assert-SourceTokens $advancedHeader @(
        "kAdvancedWorldServicesAbiVersion = 3", "Mesh = 4",
        "kTraceMeshVertexCapacity", "mesh_vertex_count", "contents64",
        "property_get_raw", "property_collection_resize"
    )
    Assert-SourceTokens $advancedAdapter @(
        "property_get_raw_complete", "property_set_raw_complete",
        "property_collection_resize_complete", "property_child_at_complete",
        "services.property_get_raw", "services.property_child_at"
    )
    Assert-SourceTokens $advancedComplete @(
        '#include "game_api_advanced_build.cpp"',
        "class CompleteTraceFilter", "build_native_ray",
        "TraceShape::Sphere", "TraceShape::Capsule", "TraceShape::Mesh",
        "trace_complete", "grenade_spawn_complete",
        "services.trace = &trace_complete",
        "services.grenade_spawn = &grenade_spawn_complete"
    )
    Assert-SourceTokens $advancedFinal @(
        '#include "game_api_advanced_complete_build.cpp"',
        "m_bIsIncGrenade", '"molotov_projectile"',
        "grenade_spawn_incendiary", "grenade_spawn_final",
        "services.grenade_spawn = &grenade_spawn_final"
    )
    Assert-SourceTokens $advancedVerified @(
        '#include "game_api_advanced_final_build.cpp"',
        "sizeof(Ray_t) == sizeof(LuaCSAdvancedApi::NativeRay)",
        "trace object_set_mask contains unsupported Source 2 bits",
        "services.trace = &trace_verified"
    )
    Assert-SourceTokens $advancedSchemaGrenades @(
        '#include "game_api_advanced_verified_build.cpp"',
        '"m_vInitialPosition"', '"m_vInitialVelocity"',
        '"m_vecOriginalSpawnLocation"', '"m_bIsLive"',
        '"m_bDetonationRecorded"', '"m_flSpawnTime"',
        '"m_flDetonateTime"', "schedule_schema_fuse",
        "services.grenade_spawn = &grenade_spawn_schema",
        "services.grenade_detonate = &grenade_detonate_schema"
    )
    Assert-SourceOmits $advancedSchemaGrenades @('"Detonate"')
    Assert-SourceTokens $propertiesSource @(
        '"get_raw"', '"set_raw"', '"collection_count"',
        '"collection_resize"', '"children"'
    )
    Assert-SourceTokens $tracesSource @(
        '"sphere"', '"capsule"', '"mesh"', '"SHAPE_MESH"',
        "SET_INT(contents64)", "SET_INT(shape_collision_function_mask)"
    )
    Assert-SourceTokens $tracesVerified @(
        '#include "traces.cpp"', '"MAX_IGNORE_ENTITIES"',
        '"MAX_MESH_VERTICES"', '"OBJECTS_ALL"'
    )
    Assert-SourceTokens $grenadesSource @(
        "thrower_entity_index", "bounce_count", "smoke_effect_tick",
        "bounce_sound"
    )

    if (-not (Test-Path -LiteralPath $advancedGamedata -PathType Leaf)) {
        throw "advanced Windows gamedata was not packaged"
    }
    $advancedText = Get-Content -LiteralPath $advancedGamedata -Raw
    foreach ($entry in @("GameTraceManager", "TraceFunc", "TraceShape")) {
        if ($advancedText -notmatch [regex]::Escape($entry)) {
            throw "advanced gamedata is missing '$entry'"
        }
    }

    if (-not (Test-Path -LiteralPath $vdf -PathType Leaf)) {
        throw "luacs2.vdf was not packaged"
    }
    if ((Get-Content -LiteralPath $vdf -Raw) -notmatch
        'addons/LuaCS/bin/win64/luacs2') {
        throw "luacs2.vdf does not point to the packaged win64 DLL"
    }
    if (-not (Test-Path -LiteralPath $compiler -PathType Leaf)) {
        throw "compile.exe was not built"
    }

    $first = (& $compiler --no-pause 2>&1 | Out-String)
    if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $output)) {
        throw "initial example_welcome.lua compilation failed`n$first"
    }
    foreach ($label in @("Code size", "Data size", "Stack/heap size",
                          "Total requirements", "Compilation time")) {
        if ($first -notmatch [regex]::Escape($label)) {
            throw "modern compiler report did not contain '$label'"
        }
    }
    $firstHash = (Get-FileHash -LiteralPath $output -Algorithm SHA256).Hash

    $second = (& $compiler --no-pause 2>&1 | Out-String)
    if ($LASTEXITCODE -ne 0 -or $second -notmatch "ALREADY COMPILED") {
        throw "unchanged source was not reported as already compiled"
    }

    $bytes = [System.IO.File]::ReadAllBytes($output)
    $bytes[$bytes.Length - 1] = $bytes[$bytes.Length - 1] -bxor 0x5A
    [System.IO.File]::WriteAllBytes($output, $bytes)
    & $compiler --no-pause
    if ($LASTEXITCODE -ne 0) {
        throw "compiler did not recover from a corrupted SMG"
    }
    $repairedHash = (Get-FileHash -LiteralPath $output -Algorithm SHA256).Hash
    if ($repairedHash -eq $firstHash) {
        throw "corrupted SMG was not replaced with a newly encrypted package"
    }

    Set-Content -LiteralPath $badSource -Encoding UTF8 -NoNewline -Value "print('valid')"
    & $compiler --no-pause $badSource
    if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $badOutput)) {
        throw "valid syntax_failure baseline did not compile"
    }
    $baselineHash = (Get-FileHash -LiteralPath $badOutput -Algorithm SHA256).Hash

    Set-Content -LiteralPath $badSource -Encoding UTF8 -NoNewline -Value "local broken = function("
    & $compiler --no-pause $badSource
    if ($LASTEXITCODE -eq 0) { throw "invalid Lua unexpectedly compiled" }
    $afterFailureHash = (Get-FileHash -LiteralPath $badOutput -Algorithm SHA256).Hash
    if ($baselineHash -ne $afterFailureHash) {
        throw "syntax error replaced the previous SMG"
    }

    Write-Host "LuaCS package, live game-event interface, all eight required Source 2 hooks, Source 2 lua command registration, plugin metadata/lifecycle, real Ray_t resolver, schema-native grenades, verified ABI v3 adapters, Lua modules, and compiler smoke tests passed."
}
finally {
    Remove-Item $output, $badOutput, $badSource, $key -Force -ErrorAction SilentlyContinue
}

exit 0
