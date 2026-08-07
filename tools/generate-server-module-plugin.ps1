param(
    [Parameter(Mandatory = $true)]
    [string]$Source,

    [Parameter(Mandatory = $true)]
    [string]$Destination
)

$ErrorActionPreference = "Stop"

function Normalize-Newlines {
    param([Parameter(Mandatory = $true)][string]$Value)
    return $Value.Replace("`r`n", "`n").Replace("`r", "`n")
}

$sourcePath = [System.IO.Path]::GetFullPath($Source)
$destinationPath = [System.IO.Path]::GetFullPath($Destination)
if (-not [System.IO.File]::Exists($sourcePath)) {
    throw "Plugin source file does not exist: $sourcePath"
}

$text = Normalize-Newlines ([System.IO.File]::ReadAllText($sourcePath))

$headerMarker = @'
#include "plugin.h"
'@
$headerMarker = $headerMarker.Replace('\"', '"')
$headerMarker = $headerMarker.TrimEnd("`r", "`n")
$headerReplacement = @'
#include "plugin.h"
#include "server_module.h"
#include <cctype>
'@
$headerReplacement = $headerReplacement.Replace('\"', '"')
$headerReplacement = $headerReplacement.TrimEnd("`r", "`n")

# Keep module binding independent from the exact contents of the subsequent
# game_api_.initialize failure block. The declaration and initializer call are
# each required exactly once, so source drift is still rejected explicitly.
$serverModuleMarker = @'
    std::string game_api_error;
'@
$serverModuleMarker = $serverModuleMarker.TrimEnd("`r", "`n")
$serverModuleReplacement = @'
    std::string server_module_error;
    if (!LuaCSBindGameServerModule(g_server, server_module_error)) {
        const std::string message =
            "CS2 game server module binding failed: " + server_module_error;
        write_console("[ERROR] (lua) " + message);
        copy_error(error, maxlen, message);
        release_lua_dependency();
        return false;
    }
    write_console("[INFO] (lua) Bound actual CS2 game server module: " +
                  path_text(LuaCSGameServerModulePath()));

    std::string game_api_error;
'@
$serverModuleReplacement = $serverModuleReplacement.Replace('\"', '"')
$serverModuleReplacement = $serverModuleReplacement.TrimEnd("`r", "`n")

$initializationMarker = @'
    if (!game_api_.initialize(root, game_api_error)) {
'@
$initializationMarker = $initializationMarker.Replace('\"', '"')
$initializationMarker = $initializationMarker.TrimEnd("`r", "`n")

$errorLogMarker = @'
    g_native_error_log = root / "logs" / "luacs-errors.log";
'@
$errorLogMarker = $errorLogMarker.Replace('\"', '"')
$errorLogMarker = $errorLogMarker.TrimEnd("`r", "`n")
$errorLogReplacement = @'
    g_native_error_log = root / "logs" / "luacs-errors.log";
    {
        std::error_code remove_error;
        std::filesystem::remove(g_native_error_log, remove_error);
        if (remove_error) {
            write_console("[WARN] (lua) Could not reset the current-session native error log: " +
                          remove_error.message());
        }
    }
'@
$errorLogReplacement = $errorLogReplacement.Replace('\"', '"')
$errorLogReplacement = $errorLogReplacement.TrimEnd("`r", "`n")

$clientCommandMarker = @'
void LuaCSPlugin::Hook_ClientCommand(CPlayerSlot slot,
                                     const CCommand& command) {
    runtime_.client_command(slot.Get(), command.GetCommandString());
}
'@
$clientCommandMarker = $clientCommandMarker.Replace('\"', '"')
$clientCommandMarker = $clientCommandMarker.TrimEnd("`r", "`n")
$clientCommandReplacement = @'
void LuaCSPlugin::Hook_ClientCommand(CPlayerSlot slot,
                                     const CCommand& command) {
    const char* command_line = command.GetCommandString();
    if (!command_line) return;
    const std::string_view raw(command_line);
    const std::size_t split = raw.find_first_of(" \t");
    std::string command_name(raw.substr(0, split));
    for (char& character : command_name) {
        character = static_cast<char>(
            std::tolower(static_cast<unsigned char>(character)));
    }
    if (command_name == "say" || command_name == "say_team") return;
    runtime_.client_command(slot.Get(), raw);
}
'@
$clientCommandReplacement = $clientCommandReplacement.Replace('\"', '"')
$clientCommandReplacement = $clientCommandReplacement.TrimEnd("`r", "`n")

$chatEventMarker = @'
    if (copy) {
        const std::uint64_t token =
            game_api_.begin_event(copy, true, dont_broadcast);
        runtime_.dispatch_game_event(token, copy->GetName(), copy->GetID(),
                                     copy->IsReliable(), copy->IsLocal(), true,
                                     dont_broadcast);
        game_api_.end_event(token);
        game_api_.free_event(copy);
    }
    RETURN_META_VALUE(MRES_IGNORED, true);
'@
$chatEventMarker = $chatEventMarker.Replace('\"', '"')
$chatEventMarker = $chatEventMarker.TrimEnd("`r", "`n")
$chatEventReplacement = @'
    if (copy) {
        const char* event_name = copy->GetName();
        if (!event_name) event_name = "";
        if (std::string_view(event_name) == "player_chat") {
            const char* chat_text = copy->GetString("text", "");
            const int player_slot = copy->GetPlayerSlot("userid").Get();
            if (chat_text && (chat_text[0] == '!' || chat_text[0] == '/') &&
                player_slot >= 0 && player_slot < 64) {
                runtime_.client_command(player_slot, chat_text);
            }
        }
        const std::uint64_t token =
            game_api_.begin_event(copy, true, dont_broadcast);
        runtime_.dispatch_game_event(token, event_name, copy->GetID(),
                                     copy->IsReliable(), copy->IsLocal(), true,
                                     dont_broadcast);
        game_api_.end_event(token);
        game_api_.free_event(copy);
    }
    RETURN_META_VALUE(MRES_IGNORED, true);
'@
$chatEventReplacement = $chatEventReplacement.Replace('\"', '"')
$chatEventReplacement = $chatEventReplacement.TrimEnd("`r", "`n")

function Assert-ExactlyOnce {
    param(
        [Parameter(Mandatory = $true)][string]$InputText,
        [Parameter(Mandatory = $true)][string]$Marker,
        [Parameter(Mandatory = $true)][string]$Label
    )

    $normalizedMarker = (Normalize-Newlines $Marker).TrimEnd("`n")
    $first = $InputText.IndexOf($normalizedMarker, [StringComparison]::Ordinal)
    if ($first -lt 0) {
        throw "$Label marker was not found in plugin.cpp"
    }
    if ($InputText.IndexOf($normalizedMarker, $first + $normalizedMarker.Length,
                           [StringComparison]::Ordinal) -ge 0) {
        throw "$Label marker appeared more than once in plugin.cpp"
    }
}

function Replace-ExactlyOnce {
    param(
        [Parameter(Mandatory = $true)][string]$InputText,
        [Parameter(Mandatory = $true)][string]$Marker,
        [Parameter(Mandatory = $true)][string]$Replacement,
        [Parameter(Mandatory = $true)][string]$Label
    )

    $normalizedMarker = (Normalize-Newlines $Marker).TrimEnd("`n")
    $normalizedReplacement = (Normalize-Newlines $Replacement).TrimEnd("`n")
    Assert-ExactlyOnce $InputText $normalizedMarker $Label
    $first = $InputText.IndexOf($normalizedMarker, [StringComparison]::Ordinal)
    return $InputText.Substring(0, $first) + $normalizedReplacement +
           $InputText.Substring($first + $normalizedMarker.Length)
}

$text = Replace-ExactlyOnce $text $headerMarker $headerReplacement "plugin header"
$text = Replace-ExactlyOnce $text $errorLogMarker $errorLogReplacement "native error log"
$text = Replace-ExactlyOnce $text $serverModuleMarker $serverModuleReplacement "server module binding"
Assert-ExactlyOnce $text $initializationMarker "game API initialization"
$text = Replace-ExactlyOnce $text $clientCommandMarker $clientCommandReplacement "ClientCommand chat guard"
$text = Replace-ExactlyOnce $text $chatEventMarker $chatEventReplacement "player_chat dispatch"

$directory = [System.IO.Path]::GetDirectoryName($destinationPath)
[System.IO.Directory]::CreateDirectory($directory) | Out-Null
[System.IO.File]::WriteAllText(
    $destinationPath,
    $text,
    [System.Text.UTF8Encoding]::new($false))

Write-Host "Generated plugin source with server-module binding and player_chat routing at $destinationPath"
