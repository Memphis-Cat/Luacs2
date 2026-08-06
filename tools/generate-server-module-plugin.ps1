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

$bindIndex = $text.IndexOf(
    'LuaCSBindGameServerModule(g_server, server_module_error)',
    [System.StringComparison]::Ordinal)
$initializeIndex = $text.IndexOf(
    'game_api_.initialize(root, game_api_error)',
    [System.StringComparison]::Ordinal)
if ($bindIndex -lt 0 -or $initializeIndex -lt 0 -or
    $bindIndex -ge $initializeIndex) {
    throw "game server module binding must happen before game API initialization"
}

$destinationDirectory = Split-Path -Parent $Destination
[System.IO.Directory]::CreateDirectory($destinationDirectory) | Out-Null
[System.IO.File]::WriteAllText(
    $Destination,
    $text,
    [System.Text.UTF8Encoding]::new($false))

Write-Host 'Generated plugin source with live CS2 server module binding.'
