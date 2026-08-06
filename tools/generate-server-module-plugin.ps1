param(
    [Parameter(Mandatory = $true)]
    [string]$Source,

    [Parameter(Mandatory = $true)]
    [string]$Destination
)

$ErrorActionPreference = "Stop"

if (-not (Test-Path -LiteralPath $Source -PathType Leaf)) {
    throw "plugin source does not exist: $Source"
}

$sourcePath = (Resolve-Path -LiteralPath $Source).Path
$text = [System.IO.File]::ReadAllText($sourcePath).Replace("`r`n", "`n")

$includeMarker = @'
#include "plugin.h"
'@.Replace("`r`n", "`n")
$includeReplacement = @'
#include "plugin.h"
#include "server_module.h"
'@.Replace("`r`n", "`n")

$includeOccurrences = ([regex]::Matches(
    $text,
    [regex]::Escape($includeMarker))).Count
if ($includeOccurrences -ne 1) {
    throw "expected exactly one plugin include marker, found $includeOccurrences"
}
$text = $text.Replace($includeMarker, $includeReplacement)

$errorLogMarker = @'
    g_native_error_log = root / "logs" / "luacs-errors.log";
'@.Replace("`r`n", "`n")
$errorLogReplacement = @'
    g_native_error_log = root / "logs" / "luacs-errors.log";
    {
        std::error_code reset_error;
        std::filesystem::remove(g_native_error_log, reset_error);
        if (reset_error) {
            std::ofstream clear_error_log(g_native_error_log,
                                          std::ios::trunc);
        }
    }
'@.Replace("`r`n", "`n")

$errorLogOccurrences = ([regex]::Matches(
    $text,
    [regex]::Escape($errorLogMarker))).Count
if ($errorLogOccurrences -ne 1) {
    throw (
        "expected exactly one native error log marker, found " +
        $errorLogOccurrences)
}
$text = $text.Replace($errorLogMarker, $errorLogReplacement)

$initializationMarker = @'
    std::string game_api_error;
    if (!game_api_.initialize(root, game_api_error)) {
'@.Replace("`r`n", "`n")
$initializationReplacement = @'
    std::string server_module_error;
    if (!LuaCSBindGameServerModule(g_server, server_module_error)) {
        const std::string message =
            "CS2 game server module binding failed: " + server_module_error;
        write_console("[ERROR] (lua) " + message);
        copy_error(error, maxlen, message);
        return false;
    }
    write_console("[INFO] (lua) Bound actual CS2 game server module: " +
                  LuaCSGameServerModulePath().string());

    std::string game_api_error;
    if (!game_api_.initialize(root, game_api_error)) {
'@.Replace("`r`n", "`n")

$initializationOccurrences = ([regex]::Matches(
    $text,
    [regex]::Escape($initializationMarker))).Count
if ($initializationOccurrences -ne 1) {
    throw (
        "expected exactly one game API initialization marker, found " +
        $initializationOccurrences)
}
$text = $text.Replace($initializationMarker, $initializationReplacement)

foreach ($required in @(
    '#include "server_module.h"',
    'std::filesystem::remove(g_native_error_log, reset_error)',
    'std::ofstream clear_error_log(g_native_error_log',
    'LuaCSBindGameServerModule(g_server, server_module_error)',
    'CS2 game server module binding failed:',
    'Bound actual CS2 game server module:',
    'LuaCSGameServerModulePath().string()',
    'game_api_.initialize(root, game_api_error)'
)) {
    if (-not $text.Contains($required)) {
        throw "generated plugin source is missing '$required'"
    }
}

$resetIndex = $text.IndexOf(
    'std::filesystem::remove(g_native_error_log, reset_error)',
    [System.StringComparison]::Ordinal)
$bindIndex = $text.IndexOf(
    'LuaCSBindGameServerModule(g_server, server_module_error)',
    [System.StringComparison]::Ordinal)
$initializeIndex = $text.IndexOf(
    'game_api_.initialize(root, game_api_error)',
    [System.StringComparison]::Ordinal)
if ($resetIndex -lt 0 -or $bindIndex -lt 0 -or $initializeIndex -lt 0 -or
    $resetIndex -ge $bindIndex -or $bindIndex -ge $initializeIndex) {
    throw (
        "native error log reset and game server module binding must happen " +
        "before game API initialization")
}

$destinationDirectory = Split-Path -Parent $Destination
[System.IO.Directory]::CreateDirectory($destinationDirectory) | Out-Null
[System.IO.File]::WriteAllText(
    $Destination,
    $text,
    [System.Text.UTF8Encoding]::new($false))

Write-Host (
    'Generated plugin source with current-session error logging and live ' +
    'CS2 server module binding.')
