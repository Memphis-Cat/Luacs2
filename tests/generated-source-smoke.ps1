$ErrorActionPreference = "Stop"

$root = (Resolve-Path "$PSScriptRoot\..").Path
$gameApiSource = Join-Path $root "src\plugin\game_api.cpp"
$pluginSource = Join-Path $root "src\plugin\plugin.cpp"
$serverModuleHeader = Join-Path $root "src\plugin\server_module.h"
$serverModuleSource = Join-Path $root "src\plugin\server_module.cpp"
$scannerGenerator = Join-Path $root "tools\generate-disk-backed-game-api.ps1"
$diagnosticInjector = Join-Path $root "tools\inject-signature-diagnostics.ps1"
$pluginGenerator = Join-Path $root "tools\generate-server-module-plugin.ps1"
$builtGeneratedGameApi = Join-Path $root "build\generated\plugin\game_api.cpp"
$builtGeneratedPlugin = Join-Path $root "build\generated\plugin\plugin.cpp"
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

    $previousPreference = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    try {
        $lines = & powershell -NoProfile -ExecutionPolicy Bypass -File $Script `
            @Arguments 2>&1
        $exitCode = $LASTEXITCODE
    }
    finally {
        $ErrorActionPreference = $previousPreference
    }

    [pscustomobject]@{
        ExitCode = $exitCode
        Output = ($lines | Out-String)
    }
}

foreach ($requiredFile in @(
    $gameApiSource,
    $pluginSource,
    $serverModuleHeader,
    $serverModuleSource,
    $scannerGenerator,
    $diagnosticInjector,
    $pluginGenerator,
    $builtGeneratedGameApi,
    $builtGeneratedPlugin
)) {
    if (-not (Test-Path -LiteralPath $requiredFile -PathType Leaf)) {
        throw "required generated-source test file is missing: $requiredFile"
    }
}

[System.IO.Directory]::CreateDirectory($tempRoot) | Out-Null
try {
    $regeneratedGameApi = Join-Path $tempRoot "game_api.cpp"
    $generation = Invoke-ScriptProcess $scannerGenerator @(
        "-Source", $gameApiSource,
        "-Destination", $regeneratedGameApi)
    if ($generation.ExitCode -ne 0) {
        throw "disk-backed scanner regeneration failed:`n$($generation.Output)"
    }

    $injection = Invoke-ScriptProcess $diagnosticInjector @(
        "-Path", $regeneratedGameApi)
    if ($injection.ExitCode -ne 0) {
        throw "signature diagnostic/module injection failed:`n$($injection.Output)"
    }

    $regeneratedPlugin = Join-Path $tempRoot "plugin.cpp"
    $pluginGeneration = Invoke-ScriptProcess $pluginGenerator @(
        "-Source", $pluginSource,
        "-Destination", $regeneratedPlugin)
    if ($pluginGeneration.ExitCode -ne 0) {
        throw "server-module plugin generation failed:`n$($pluginGeneration.Output)"
    }

    $expectedGameApiHash = (
        Get-FileHash -LiteralPath $builtGeneratedGameApi -Algorithm SHA256).Hash
    $actualGameApiHash = (
        Get-FileHash -LiteralPath $regeneratedGameApi -Algorithm SHA256).Hash
    if ($expectedGameApiHash -ne $actualGameApiHash) {
        throw (
            "the checked build generated a different game_api.cpp than the " +
            "current strict generators; rebuild before deployment")
    }

    $expectedPluginHash = (
        Get-FileHash -LiteralPath $builtGeneratedPlugin -Algorithm SHA256).Hash
    $actualPluginHash = (
        Get-FileHash -LiteralPath $regeneratedPlugin -Algorithm SHA256).Hash
    if ($expectedPluginHash -ne $actualPluginHash) {
        throw (
            "the checked build generated a different plugin.cpp than the " +
            "current strict generator; rebuild before deployment")
    }

    $generatedGameApiText = [System.IO.File]::ReadAllText($regeneratedGameApi)
    Assert-Contains $generatedGameApiText @(
        '#include "server_module.h"',
        "thread_local std::string g_pattern_scan_diagnostic;",
        "GetModuleFileNameW",
        "IMAGE_SCN_MEM_EXECUTE",
        "PointerToRawData",
        "live_base + rva",
        "LuaCSGameServerModule()",
        "LuaCSGameServerModulePath().string()",
        "Selected game server module:",
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
    Assert-Omits $generatedGameApiText @(
        'GetModuleHandleW(L"server.dll")',
        "offset <= image_size - pattern.size()",
        "std::vector<const char*> missing_functions;",
        "could not resolve one or more required CS2 functions"
    ) "generated game API"

    $generatedPluginText = [System.IO.File]::ReadAllText($regeneratedPlugin)
    Assert-Contains $generatedPluginText @(
        '#include "server_module.h"',
        "LuaCSBindGameServerModule(g_server, server_module_error)",
        "CS2 game server module binding failed:",
        "Bound actual CS2 game server module:",
        "LuaCSGameServerModulePath().string()",
        "game_api_.initialize(root, game_api_error)"
    ) "generated plugin"
    $bindIndex = $generatedPluginText.IndexOf(
        "LuaCSBindGameServerModule(g_server, server_module_error)",
        [System.StringComparison]::Ordinal)
    $initializeIndex = $generatedPluginText.IndexOf(
        "game_api_.initialize(root, game_api_error)",
        [System.StringComparison]::Ordinal)
    if ($bindIndex -lt 0 -or $initializeIndex -lt 0 -or
        $bindIndex -ge $initializeIndex) {
        throw "generated plugin initializes the game API before module binding"
    }

    $serverModuleText = [System.IO.File]::ReadAllText($serverModuleSource)
    Assert-Contains $serverModuleText @(
        "*reinterpret_cast<void***>(server_interface)",
        "vtable[0]",
        "GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS",
        "GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT",
        "GetModuleFileNameW",
        'L"server.dll"',
        'L"\\addons\\metamod\\"',
        "proxy server.dll instead of CS2's game server module",
        "g_game_server_module = module",
        "g_game_server_module_path = resolved_path"
    ) "game server module binder"

    $driftedSource = Join-Path $tempRoot "game_api_drifted.cpp"
    $sourceText = [System.IO.File]::ReadAllText($gameApiSource)
    $driftedText = $sourceText.Replace(
        "void* find_pattern(HMODULE module, std::string_view text)",
        "void* find_pattern_drifted(HMODULE module, std::string_view text)")
    [System.IO.File]::WriteAllText(
        $driftedSource,
        $driftedText,
        [System.Text.UTF8Encoding]::new($false))
    $driftedResult = Invoke-ScriptProcess $scannerGenerator @(
        "-Source", $driftedSource,
        "-Destination", (Join-Path $tempRoot "drifted-output.cpp"))
    if ($driftedResult.ExitCode -eq 0) {
        throw "scanner generator accepted source drift"
    }
    if ($driftedResult.Output -notmatch "expected exactly one legacy") {
        throw "scanner source-drift failure was not explicit"
    }

    $brokenGenerated = Join-Path $tempRoot "broken-generated.cpp"
    $brokenText = $generatedGameApiText.Replace(
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

    $driftedPlugin = Join-Path $tempRoot "plugin-drifted.cpp"
    $pluginText = [System.IO.File]::ReadAllText($pluginSource)
    $driftedPluginText = $pluginText.Replace(
        "game_api_.initialize(root, game_api_error)",
        "game_api_.initialize_drifted(root, game_api_error)")
    [System.IO.File]::WriteAllText(
        $driftedPlugin,
        $driftedPluginText,
        [System.Text.UTF8Encoding]::new($false))
    $driftedPluginResult = Invoke-ScriptProcess $pluginGenerator @(
        "-Source", $driftedPlugin,
        "-Destination", (Join-Path $tempRoot "plugin-drifted-output.cpp"))
    if ($driftedPluginResult.ExitCode -eq 0) {
        throw "plugin generator accepted initialization source drift"
    }
    if ($driftedPluginResult.Output -notmatch "initialization marker") {
        throw "plugin source-drift failure was not explicit"
    }

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
        "LuaCS generated-source tests passed: deterministic game API and " +
        "plugin generation, actual server-module binding, Metamod proxy " +
        "rejection, deep per-signature diagnostics, strict source-drift " +
        "rejection, and LuaCS-only native error logging.")
}
finally {
    Remove-Item -LiteralPath $tempRoot -Recurse -Force -ErrorAction SilentlyContinue
}
