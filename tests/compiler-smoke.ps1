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
$cmakeFile = Join-Path $root "CMakeLists.txt"
$advancedHeader = Join-Path $root "include\luacs\advanced_world_api.h"
$advancedAdapter = Join-Path $root "src\plugin\game_api_advanced_build.cpp"
$advancedComplete = Join-Path $root "src\plugin\game_api_advanced_complete_build.cpp"
$advancedFinal = Join-Path $root "src\plugin\game_api_advanced_final_build.cpp"
$propertiesSource = Join-Path $root "src\modules\properties\properties.cpp"
$tracesSource = Join-Path $root "src\modules\traces\traces.cpp"
$grenadesSource = Join-Path $root "src\modules\grenades\grenades.cpp"

function Assert-SourceTokens {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string[]]$Tokens
    )

    if (-not (Test-Path $Path)) {
        throw "required source file is missing: $Path"
    }
    $text = Get-Content $Path -Raw
    foreach ($token in $Tokens) {
        if ($text -notmatch [regex]::Escape($token)) {
            throw "source file '$Path' is missing '$token'"
        }
    }
}

try {
    $requiredNativeFiles = @(
        "luacs2.dll",
        "lua55.dll",
        "events.dll",
        "timers.dll",
        "players.dll",
        "commands.dll",
        "math.dll",
        "weapons.dll",
        "hud.dll",
        "cvars.dll",
        "teams.dll",
        "rounds.dll",
        "entities.dll",
        "sounds.dll",
        "properties.dll",
        "traces.dll",
        "grenades.dll"
    )
    foreach ($name in $requiredNativeFiles) {
        $path = Join-Path $bin $name
        if (-not (Test-Path $path)) {
            throw "required native file is missing from bin\win64: $name"
        }
    }

    Assert-SourceTokens $cmakeFile @(
        "src/plugin/game_api_advanced_final_build.cpp",
        "add_luacs_module(traces src/modules/traces/traces.cpp)"
    )
    Assert-SourceTokens $advancedHeader @(
        "kAdvancedWorldServicesAbiVersion = 3",
        "Mesh = 4",
        "kTraceMeshVertexCapacity",
        "mesh_vertex_count",
        "contents64",
        "property_get_raw",
        "property_collection_resize"
    )
    Assert-SourceTokens $advancedAdapter @(
        "property_get_raw_complete",
        "property_set_raw_complete",
        "property_collection_resize_complete",
        "property_child_at_complete",
        "services.property_get_raw",
        "services.property_child_at"
    )
    Assert-SourceTokens $advancedComplete @(
        '#include "game_api_advanced_build.cpp"',
        "class CompleteTraceFilter",
        "build_native_ray",
        "TraceShape::Sphere",
        "TraceShape::Capsule",
        "TraceShape::Mesh",
        "trace_complete",
        "grenade_spawn_complete",
        "services.trace = &trace_complete",
        "services.grenade_spawn = &grenade_spawn_complete"
    )
    Assert-SourceTokens $advancedFinal @(
        '#include "game_api_advanced_complete_build.cpp"',
        "m_bIsIncGrenade",
        '"molotov_projectile"',
        "grenade_spawn_incendiary",
        "grenade_spawn_final",
        "services.grenade_spawn = &grenade_spawn_final"
    )
    Assert-SourceTokens $propertiesSource @(
        '"get_raw"',
        '"set_raw"',
        '"collection_count"',
        '"collection_resize"',
        '"children"'
    )
    Assert-SourceTokens $tracesSource @(
        '"sphere"',
        '"capsule"',
        '"mesh"',
        '"SHAPE_MESH"',
        "SET_INT(contents64)",
        "SET_INT(shape_collision_function_mask)"
    )
    Assert-SourceTokens $grenadesSource @(
        "thrower_entity_index",
        "bounce_count",
        "smoke_effect_tick",
        "bounce_sound"
    )

    if (-not (Test-Path $advancedGamedata)) {
        throw "advanced Windows gamedata was not packaged"
    }
    $advancedText = Get-Content $advancedGamedata -Raw
    foreach ($entry in @("GameTraceManager", "TraceFunc", "TraceShape")) {
        if ($advancedText -notmatch [regex]::Escape($entry)) {
            throw "advanced gamedata is missing '$entry'"
        }
    }

    if (-not (Test-Path $vdf)) { throw "luacs2.vdf was not packaged" }
    $vdfText = Get-Content $vdf -Raw
    if ($vdfText -notmatch 'addons/LuaCS/bin/win64/luacs2') {
        throw "luacs2.vdf does not point to the packaged win64 DLL"
    }

    if (-not (Test-Path $compiler)) { throw "compile.exe was not built" }

    $first = (& $compiler --no-pause 2>&1 | Out-String)
    if ($LASTEXITCODE -ne 0 -or -not (Test-Path $output)) {
        throw "initial example_welcome.lua compilation failed`n$first"
    }
    foreach ($label in @("Code size", "Data size", "Stack/heap size",
                          "Total requirements", "Compilation time")) {
        if ($first -notmatch [regex]::Escape($label)) {
            throw "modern compiler report did not contain '$label'"
        }
    }
    $firstHash = (Get-FileHash $output -Algorithm SHA256).Hash

    $second = (& $compiler --no-pause 2>&1 | Out-String)
    if ($LASTEXITCODE -ne 0 -or $second -notmatch "ALREADY COMPILED") {
        throw "unchanged source was not reported as already compiled"
    }

    $bytes = [System.IO.File]::ReadAllBytes($output)
    $bytes[$bytes.Length - 1] = $bytes[$bytes.Length - 1] -bxor 0x5A
    [System.IO.File]::WriteAllBytes($output, $bytes)
    & $compiler --no-pause
    if ($LASTEXITCODE -ne 0) { throw "compiler did not recover from a corrupted SMG" }
    $repairedHash = (Get-FileHash $output -Algorithm SHA256).Hash
    if ($repairedHash -eq $firstHash) {
        throw "corrupted SMG was not replaced with a newly encrypted package"
    }

    Set-Content -Path $badSource -Encoding UTF8 -NoNewline -Value "print('valid')"
    & $compiler --no-pause $badSource
    if ($LASTEXITCODE -ne 0 -or -not (Test-Path $badOutput)) {
        throw "valid syntax_failure baseline did not compile"
    }
    $baselineHash = (Get-FileHash $badOutput -Algorithm SHA256).Hash

    Set-Content -Path $badSource -Encoding UTF8 -NoNewline -Value "local broken = function("
    & $compiler --no-pause $badSource
    if ($LASTEXITCODE -eq 0) { throw "invalid Lua unexpectedly compiled" }
    $afterFailureHash = (Get-FileHash $badOutput -Algorithm SHA256).Hash
    if ($baselineHash -ne $afterFailureHash) {
        throw "syntax error replaced the previous SMG"
    }

    Write-Host "LuaCS package, final ABI v3 adapters, Lua modules, and compiler smoke tests passed."
}
finally {
    Remove-Item $output, $badOutput, $badSource, $key -Force -ErrorAction SilentlyContinue
}

exit 0