$ErrorActionPreference = "Stop"

$root = (Resolve-Path "$PSScriptRoot\..").Path
$source = Join-Path $root "src\plugin\game_api.cpp"
$pluginSource = Join-Path $root "src\plugin\plugin.cpp"
$generator = Join-Path $root "tools\generate-disk-backed-game-api.ps1"
$diagnosticInjector = Join-Path $root "tools\inject-signature-diagnostics.ps1"
$builtGenerated = Join-Path $root "build\generated\plugin\game_api.cpp"
$tempRoot = Join-Path ([System.IO.Path]::GetTempPath()) (
    "luacs-generated-smoke-" + [Guid]::NewGuid().ToString("N"))

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
            throw "$Context still contains obsolete token '$token'"
        }
    }
}

function Invoke-ScriptProcess {
    param(
        [Parameter(Mandatory = $true)][string]$Script,
        [Parameter(Mandatory = $true)][string[]]$Arguments
    )
    $output = (& powershell -NoProfile -ExecutionPolicy Bypass -File $Script @Arguments 2>&1 |
        Out-String)
    [pscustomobject]@{ ExitCode = $LASTEXITCODE; Output = $output }
}

foreach ($requiredFile in @(
    $source,
    $pluginSource,
    $generator,
    $diagnosticInjector,
    $builtGenerated
)) {
    if (-not (Test-Path -LiteralPath $requiredFile -PathType Leaf)) {
        throw "required generated-source test file is missing: $requiredFile"
    }
}

[System.IO.Directory]::CreateDirectory($tempRoot) | Out-Null
try {
    $regenerated = Join-Path $tempRoot "game_api.cpp"
    $generation = Invoke-ScriptProcess $generator @(
        "-Source", $source,
        "-Destination", $regenerated)
    if ($generation.ExitCode -ne 0) {
        throw "disk-backed scanner regeneration failed:`n$($generation.Output)"
    }

    $injection = Invoke-ScriptProcess $diagnosticInjector @("-Path", $regenerated)
    if ($injection.ExitCode -ne 0) {
        throw "signature diagnostic injection failed:`n$($injection.Output)"
    }

    $expectedHash = (Get-FileHash -LiteralPath $builtGenerated -Algorithm SHA256).Hash
    $actualHash = (Get-FileHash -LiteralPath $regenerated -Algorithm SHA256).Hash
    if ($expectedHash -ne $actualHash) {
        throw (
            "the checked build generated a different game_api.cpp than the " +
            "current strict generators; rebuild before deployment")
    }

    $generatedText = [System.IO.File]::ReadAllText($regenerated)
    Assert-Contains $generatedText @(
        "thread_local std::string g_pattern_scan_diagnostic;",
        "GetModuleFileNameW",
        "IMAGE_SCN_MEM_EXECUTE",
        "PointerToRawData",
        "live_base + rva",
        "pattern-bytes=",
        "executable-sections=",
        "executable-bytes-scanned=",
        "Signature scan diagnostics:",
        "Gamedata directory:",
        'record_signature("ClientPrint"',
        'record_signature("UTIL_ClientPrintAll"',
        'record_signature("CBasePlayerPawn_RemovePlayerItem"',
        'record_signature("UTIL_Remove"',
        'record_signature("CCSPlayerController_SwitchTeam"',
        'record_signature("CBasePlayerController_SetPawn"'
    ) "generated game API"
    Assert-Omits $generatedText @(
        "offset <= image_size - pattern.size()",
        "std::vector<const char*> missing_functions;",
        "could not resolve one or more required CS2 functions"
    ) "generated game API"

    $driftedSource = Join-Path $tempRoot "game_api_drifted.cpp"
    $sourceText = [System.IO.File]::ReadAllText($source)
    $driftedText = $sourceText.Replace(
        "void* find_pattern(HMODULE module, std::string_view text)",
        "void* find_pattern_drifted(HMODULE module, std::string_view text)")
    [System.IO.File]::WriteAllText(
        $driftedSource,
        $driftedText,
        [System.Text.UTF8Encoding]::new($false))
    $driftedResult = Invoke-ScriptProcess $generator @(
        "-Source", $driftedSource,
        "-Destination", (Join-Path $tempRoot "drifted-output.cpp"))
    if ($driftedResult.ExitCode -eq 0) {
        throw "scanner generator accepted source drift"
    }
    if ($driftedResult.Output -notmatch "expected exactly one legacy") {
        throw "scanner source-drift failure was not explicit"
    }

    $brokenGenerated = Join-Path $tempRoot "broken-generated.cpp"
    $brokenText = $generatedText.Replace(
        "std::vector<std::string> missing_functions;",
        "std::vector<std::string> changed_missing_functions;")
    [System.IO.File]::WriteAllText(
        $brokenGenerated,
        $brokenText,
        [System.Text.UTF8Encoding]::new($false))
    $brokenInjection = Invoke-ScriptProcess $diagnosticInjector @(
        "-Path", $brokenGenerated)
    if ($brokenInjection.ExitCode -eq 0) {
        throw "signature diagnostic injector accepted an already-modified block"
    }

    $pluginText = [System.IO.File]::ReadAllText($pluginSource)
    Assert-Contains $pluginText @(
        'g_native_error_log = root / "logs" / "luacs-errors.log";',
        'line.find("[ERROR]") == std::string_view::npos',
        'std::ofstream output(g_native_error_log, std::ios::app);',
        'GetLocalTime(&now);',
        'append_native_error(line);'
    ) "native LuaCS error logger"
    Assert-Omits $pluginText @(
        "counterstrikesharp",
        "[META]"
    ) "native LuaCS error logger"

    Write-Host (
        "LuaCS generated-source tests passed: deterministic disk scanner, " +
        "deep per-signature diagnostics, strict source-drift rejection, " +
        "strict diagnostic-block rejection, and LuaCS-only native error logging.")
}
finally {
    Remove-Item -LiteralPath $tempRoot -Recurse -Force -ErrorAction SilentlyContinue
}
