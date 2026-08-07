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
            if (clear_error_log) clear_error_log.close();
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

$clientCommandMarker = @'
void LuaCSPlugin::Hook_ClientCommand(CPlayerSlot slot,
                                     const CCommand& command) {
    runtime_.client_command(slot.Get(), command.GetCommandString());
}
'@.Replace("`r`n", "`n")
$clientCommandReplacement = @'
void LuaCSPlugin::Hook_ClientCommand(CPlayerSlot slot,
                                     const CCommand& command) {
    const char* command_line = command.GetCommandString();
    if (!command_line) return;

    const std::string_view raw(command_line);
    const auto separator = raw.find_first_of(" \t");
    const std::string_view command_name = raw.substr(0, separator);

    // Source 2 chat is routed from the player_chat game event below. Some CS2
    // builds do not expose say/say_team through ClientCommand at all, while
    // others may expose both paths. Never dispatch chat here so a command
    // cannot run twice when both engine paths are present.
    if (command_name == "say" || command_name == "say_team") return;

    runtime_.client_command(slot.Get(), raw);
}
'@.Replace("`r`n", "`n")

$clientCommandOccurrences = ([regex]::Matches(
    $text,
    [regex]::Escape($clientCommandMarker))).Count
if ($clientCommandOccurrences -ne 1) {
    throw (
        "expected exactly one ClientCommand routing marker, found " +
        $clientCommandOccurrences)
}
$text = $text.Replace($clientCommandMarker, $clientCommandReplacement)

$chatEventMarker = @'
        const std::uint64_t token =
            game_api_.begin_event(copy, true, dont_broadcast);
        runtime_.dispatch_game_event(token, copy->GetName(), copy->GetID(),
'@.Replace("`r`n", "`n")
$chatEventReplacement = @'
        const std::uint64_t token =
            game_api_.begin_event(copy, true, dont_broadcast);

        const char* event_name = copy->GetName();
        if (event_name && std::string_view(event_name) == "player_chat") {
            const char* chat_text = copy->GetString("text", "");
            if (chat_text && (chat_text[0] == '!' || chat_text[0] == '/')) {
                const int player_slot = copy->GetPlayerSlot("userid").Get();
                if (player_slot >= 0 && player_slot < 64) {
                    runtime_.client_command(player_slot, chat_text);
                } else {
                    write_console(
                        "[WARN] (lua) player_chat command had an invalid "
                        "player slot: " + std::to_string(player_slot));
                }
            }
        }

        runtime_.dispatch_game_event(token, event_name, copy->GetID(),
'@.Replace("`r`n", "`n")

$chatEventOccurrences = ([regex]::Matches(
    $text,
    [regex]::Escape($chatEventMarker))).Count
if ($chatEventOccurrences -ne 1) {
    throw (
        "expected exactly one post player_chat routing marker, found " +
        $chatEventOccurrences)
}
$text = $text.Replace($chatEventMarker, $chatEventReplacement)

foreach ($required in @(
    '#include "server_module.h"',
    'std::filesystem::remove(g_native_error_log, reset_error)',
    'std::ofstream clear_error_log(g_native_error_log',
    'if (clear_error_log) clear_error_log.close();',
    'LuaCSBindGameServerModule(g_server, server_module_error)',
    'CS2 game server module binding failed:',
    'Bound actual CS2 game server module:',
    'LuaCSGameServerModulePath().string()',
    'game_api_.initialize(root, game_api_error)',
    'if (command_name == "say" || command_name == "say_team") return;',
    'std::string_view(event_name) == "player_chat"',
    'copy->GetString("text", "")',
    'copy->GetPlayerSlot("userid").Get()',
    'runtime_.client_command(player_slot, chat_text)'
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

$clientChatSkipIndex = $text.IndexOf(
    'if (command_name == "say" || command_name == "say_team") return;',
    [System.StringComparison]::Ordinal)
$playerChatIndex = $text.IndexOf(
    'std::string_view(event_name) == "player_chat"',
    [System.StringComparison]::Ordinal)
$chatDispatchIndex = $text.IndexOf(
    'runtime_.client_command(player_slot, chat_text)',
    [System.StringComparison]::Ordinal)
$eventDispatchIndex = $text.IndexOf(
    'runtime_.dispatch_game_event(token, event_name, copy->GetID()',
    [System.StringComparison]::Ordinal)
if ($clientChatSkipIndex -lt 0 -or $playerChatIndex -lt 0 -or
    $chatDispatchIndex -lt 0 -or $eventDispatchIndex -lt 0 -or
    $playerChatIndex -ge $chatDispatchIndex -or
    $chatDispatchIndex -ge $eventDispatchIndex) {
    throw "generated Source 2 chat command routing is incomplete or misordered"
}

$destinationDirectory = Split-Path -Parent $Destination
[System.IO.Directory]::CreateDirectory($destinationDirectory) | Out-Null
[System.IO.File]::WriteAllText(
    $Destination,
    $text,
    [System.Text.UTF8Encoding]::new($false))

Write-Host (
    'Generated plugin source with current-session error logging, live CS2 ' +
    'server module binding, and player_chat command routing.')
