$ErrorActionPreference = "Stop"

$root = (Resolve-Path "$PSScriptRoot\..").Path
$compiler = Join-Path $root "build\package\game\csgo\addons\LuaCS\scripting\compile.exe"
$plugins = Join-Path $root "build\package\game\csgo\addons\LuaCS\plugins"
$config = Join-Path $root "build\package\game\csgo\addons\LuaCS\config"
$live = Join-Path $root "tests\live"
$key = Join-Path $config "luacs.key"
$temp = Join-Path ([System.IO.Path]::GetTempPath()) (
    "luacs-live-test-pack-smoke-" + [Guid]::NewGuid().ToString("N"))

$sources = @(
    "apitest.lua",
    "apitest_survivor.lua",
    "apitest_callback_crash.lua",
    "apitest_startup_crash.lua",
    "apitest_unload_crash.lua"
)

if (-not (Test-Path -LiteralPath $compiler -PathType Leaf)) {
    throw "live test pack smoke requires a built compile.exe: $compiler"
}

[System.IO.Directory]::CreateDirectory($temp) | Out-Null
$backups = @{}
$keyBackup = $null
$keyExisted = Test-Path -LiteralPath $key -PathType Leaf
if ($keyExisted) {
    $keyBackup = [System.IO.File]::ReadAllBytes($key)
}

try {
    foreach ($name in $sources) {
        $source = Join-Path $live $name
        if (-not (Test-Path -LiteralPath $source -PathType Leaf)) {
            throw "live test source is missing: $source"
        }

        $base = [System.IO.Path]::GetFileNameWithoutExtension($name)
        $output = Join-Path $plugins ($base + ".smg")
        if (-not $backups.ContainsKey($output)) {
            if (Test-Path -LiteralPath $output -PathType Leaf) {
                $backup = Join-Path $temp ([Guid]::NewGuid().ToString("N") + ".smg")
                Copy-Item -LiteralPath $output -Destination $backup
                $backups[$output] = $backup
            } else {
                $backups[$output] = $null
            }
        }

        $result = (& $compiler --no-pause $source 2>&1 | Out-String)
        if ($LASTEXITCODE -ne 0) {
            throw "live test source failed compiler/semantic audit: $name`n$result"
        }
        if (-not (Test-Path -LiteralPath $output -PathType Leaf)) {
            throw "compiler succeeded but did not create live test package: $output"
        }
    }

    $controllerText = [System.IO.File]::ReadAllText((Join-Path $live "apitest.lua"))
    foreach ($required in @(
        'commands.on("lua_test"',
        'events.on_post("player_death"',
        'weapon:set_clip1(before)',
        'weapon:set_reserve1(before)',
        'weapons.get_ammo(target, ammo_type)',
        'weapons.set_ammo(target, ammo_type, before)',
        'properties.set_int(target.pawn_index, "m_iHealth", before)',
        'traces.line(start_pos, end_pos, { ignore = target })',
        'grenades.count()',
        'infinite-loop watchdog'
    )) {
        if (-not $controllerText.Contains($required)) {
            throw "live API controller is missing required coverage token: $required"
        }
    }

    Write-Host (
        "LuaCS live test pack smoke passed: controller, survivor, callback " +
        "crash, startup crash, and unload crash sources all passed the real " +
        "compiler/semantic/SMG pipeline; death events, magazine/reserve/ammo, " +
        "properties, traces, grenades, and unsupported watchdog coverage are " +
        "represented in the controller.")
}
finally {
    foreach ($entry in $backups.GetEnumerator()) {
        if ($null -eq $entry.Value) {
            Remove-Item -LiteralPath $entry.Key -Force -ErrorAction SilentlyContinue
        } else {
            Copy-Item -LiteralPath $entry.Value -Destination $entry.Key -Force
        }
    }
    if ($keyExisted) {
        [System.IO.File]::WriteAllBytes($key, $keyBackup)
    } else {
        Remove-Item -LiteralPath $key -Force -ErrorAction SilentlyContinue
    }
    Remove-Item -LiteralPath $temp -Recurse -Force -ErrorAction SilentlyContinue
}

exit 0
