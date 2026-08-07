$ErrorActionPreference = "Stop"

$utf8 = [System.Text.UTF8Encoding]::new($false)
[Console]::InputEncoding = $utf8
[Console]::OutputEncoding = $utf8
$global:OutputEncoding = $utf8

$root = (Resolve-Path "$PSScriptRoot\..").Path
$generatedPlugin = Join-Path $root "build\generated\plugin\plugin.cpp"
$exampleSource = Join-Path $root "packaging\LuaCS\scripting\example_welcome.lua"
$compiler = Join-Path $root "build\package\game\csgo\addons\LuaCS\scripting\compile.exe"
$packagedSource = Join-Path $root "build\package\game\csgo\addons\LuaCS\scripting\example_welcome.lua"
$compiledOutput = Join-Path $root "build\package\game\csgo\addons\LuaCS\plugins\example_welcome.smg"
$key = Join-Path $root "build\package\game\csgo\addons\LuaCS\config\luacs.key"

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

function Assert-Omits {
    param(
        [Parameter(Mandatory = $true)][string]$Text,
        [Parameter(Mandatory = $true)][string[]]$Tokens,
        [Parameter(Mandatory = $true)][string]$Context
    )
    foreach ($token in $Tokens) {
        if ($Text.Contains($token)) {
            throw "$Context still contains '$token'"
        }
    }
}

foreach ($required in @(
    $generatedPlugin,
    $exampleSource,
    $compiler,
    $packagedSource
)) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) {
        throw "required logging/encoding test file is missing: $required"
    }
}

$generatedPluginText = [System.IO.File]::ReadAllText($generatedPlugin)
Assert-Contains $generatedPluginText @(
    'g_native_error_log = root / "logs" / "luacs-errors.log";',
    'std::filesystem::remove(g_native_error_log, reset_error);',
    'std::ofstream clear_error_log(g_native_error_log,',
    'std::ios::trunc',
    'if (clear_error_log) clear_error_log.close();',
    'append_native_error(line);',
    'line.find("[ERROR]") == std::string_view::npos'
) "generated current-session native error logger"

$resetIndex = $generatedPluginText.IndexOf(
    'std::filesystem::remove(g_native_error_log, reset_error);',
    [System.StringComparison]::Ordinal)
$initializeIndex = $generatedPluginText.IndexOf(
    'game_api_.initialize(root, game_api_error)',
    [System.StringComparison]::Ordinal)
if ($resetIndex -lt 0 -or $initializeIndex -lt 0 -or
    $resetIndex -ge $initializeIndex) {
    throw "native error log is not reset before game API initialization"
}

$exampleText = [System.IO.File]::ReadAllText($exampleSource)
Assert-Contains $exampleText @(
    'local timers = require("cs2.timers")',
    'local spawn_generation = {}',
    'local configured_pawn_handle = {}',
    'local MAX_SPAWN_ATTEMPTS = 20',
    'events.on_post("player_spawn"',
    'timers.after(SPAWN_RETRY_DELAY',
    'player:refresh()',
    'player.has_pawn',
    'spawn_generation[player.slot] ~= generation',
    'configured_pawn_handle[player.slot] == player.pawn_handle',
    'configured_pawn_handle[player.slot] = player.pawn_handle',
    'warn_operation("spawn setup"'
) "deferred example spawn setup"
Assert-Omits $exampleText @(
    'events.on("player_spawn"',
    'print("[WARN] weapon_ak47 failed:", give_error)'
) "deferred example spawn setup"

try {
    $compilerOutput = (& $compiler --no-pause $packagedSource 2>&1 |
        Out-String)
    if ($LASTEXITCODE -ne 0) {
        throw "compiler encoding probe failed:`n$compilerOutput"
    }

    Assert-Contains $compilerOutput @(
        "LuaCS Compiler",
        "authenticated SMG bytecode",
        "Build summary"
    ) "compiler output"

    foreach ($badSequence in @(
        "Γò", "Γö", "ΓÇ", "Γù", "Ã", "â", [string][char]0xFFFD
    )) {
        if ($compilerOutput.Contains($badSequence)) {
            throw (
                "compiler output contains mojibake sequence '$badSequence':`n" +
                $compilerOutput)
        }
    }
}
finally {
    Remove-Item -LiteralPath $compiledOutput, $key -Force `
        -ErrorAction SilentlyContinue
}

Write-Host (
    "LuaCS logging and encoding tests passed: UTF-8 compiler output, no " +
    "mojibake, current-session-only native errors, deferred post-spawn pawn " +
    "setup, bounded retries, generation deduplication, and one setup per pawn " +
    "handle.")
