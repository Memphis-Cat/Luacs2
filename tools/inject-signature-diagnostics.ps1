param(
    [Parameter(Mandatory = $true)]
    [string]$Path
)

$ErrorActionPreference = 'Stop'

if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
    throw "generated game API source does not exist: $Path"
}

$resolvedPath = (Resolve-Path -LiteralPath $Path).Path
$text = [System.IO.File]::ReadAllText($resolvedPath).Replace("`r`n", "`n")

$includeMarker = @'
#include "game_api_internal.h"
'@.Replace("`r`n", "`n")
$includeReplacement = @'
#include "game_api_internal.h"
#include "server_module.h"
'@.Replace("`r`n", "`n")
$includeOccurrences = ([regex]::Matches(
    $text,
    [regex]::Escape($includeMarker))).Count
if ($includeOccurrences -ne 1) {
    throw "expected exactly one game API include marker, found $includeOccurrences"
}
$text = $text.Replace($includeMarker, $includeReplacement)

$moduleMarker = @'
    const HMODULE server_module = GetModuleHandleW(L"server.dll");
    if (!server_module) {
        error = "server.dll is not loaded";
        return false;
    }
'@.Replace("`r`n", "`n")
$moduleReplacement = @'
    const HMODULE server_module =
        static_cast<HMODULE>(LuaCSGameServerModule());
    if (!server_module) {
        error = "actual CS2 game server module was not bound from the live "
                "IServerGameDLL interface";
        return false;
    }
'@.Replace("`r`n", "`n")
$moduleOccurrences = ([regex]::Matches(
    $text,
    [regex]::Escape($moduleMarker))).Count
if ($moduleOccurrences -ne 1) {
    throw "expected exactly one basename server.dll lookup, found $moduleOccurrences"
}
$text = $text.Replace($moduleMarker, $moduleReplacement)

$startMarker = @'
    impl_->client_print = reinterpret_cast<LuaCSGameApiImpl::ClientPrintFn>(
'@.Replace("`r`n", "`n")
$endMarker = @'
    if (!impl_->current_entity_system()) {
'@.Replace("`r`n", "`n")

$start = $text.IndexOf($startMarker, [System.StringComparison]::Ordinal)
if ($start -lt 0) {
    throw 'could not find the start of the CS2 signature resolution block'
}
if ($text.IndexOf($startMarker, $start + 1, [System.StringComparison]::Ordinal) -ge 0) {
    throw 'found more than one CS2 signature resolution block start'
}

$end = $text.IndexOf($endMarker, $start, [System.StringComparison]::Ordinal)
if ($end -lt 0) {
    throw 'could not find the end of the CS2 signature resolution block'
}

$legacyBlock = $text.Substring($start, $end - $start)
foreach ($required in @(
    'std::vector<const char*> missing_functions;',
    'ClientPrintFn',
    'ClientPrintAllFn',
    'RemovePlayerItemFn',
    'RemoveEntityFn',
    'SwitchTeamFn',
    'SetPawnFn',
    'could not resolve required CS2 function signature(s):'
)) {
    if (-not $legacyBlock.Contains($required)) {
        throw "signature resolution block drifted; missing '$required'"
    }
}

$replacement = @'
    std::vector<std::string> missing_functions;
    std::vector<std::string> signature_diagnostics;
    const auto record_signature = [&](const char* name,
                                      std::string_view pattern_text,
                                      bool resolved) {
        if (resolved) return;
        missing_functions.emplace_back(name);

        const auto parsed_pattern = parse_pattern(pattern_text);
        std::ostringstream detail;
        detail << name << " [pattern-bytes=" << parsed_pattern.size()
               << "; scanner="
               << (luacs_game_internal::g_pattern_scan_diagnostic.empty()
                       ? "no scanner diagnostic was produced"
                       : luacs_game_internal::g_pattern_scan_diagnostic)
               << "]";
        signature_diagnostics.push_back(detail.str());
    };

    impl_->client_print = reinterpret_cast<LuaCSGameApiImpl::ClientPrintFn>(
        luacs_game_internal::find_pattern(server_module,
                                          *client_print_pattern));
    record_signature("ClientPrint", *client_print_pattern,
                     impl_->client_print != nullptr);

    impl_->client_print_all =
        reinterpret_cast<LuaCSGameApiImpl::ClientPrintAllFn>(
            luacs_game_internal::find_pattern(server_module,
                                              *client_print_all_pattern));
    record_signature("UTIL_ClientPrintAll", *client_print_all_pattern,
                     impl_->client_print_all != nullptr);

    impl_->remove_player_item =
        reinterpret_cast<LuaCSGameApiImpl::RemovePlayerItemFn>(
            luacs_game_internal::find_pattern(server_module,
                                              *remove_player_item_pattern));
    record_signature("CBasePlayerPawn_RemovePlayerItem",
                     *remove_player_item_pattern,
                     impl_->remove_player_item != nullptr);

    impl_->remove_entity = reinterpret_cast<LuaCSGameApiImpl::RemoveEntityFn>(
        luacs_game_internal::find_pattern(server_module,
                                          *remove_entity_pattern));
    record_signature("UTIL_Remove", *remove_entity_pattern,
                     impl_->remove_entity != nullptr);

    impl_->switch_team = reinterpret_cast<LuaCSGameApiImpl::SwitchTeamFn>(
        luacs_game_internal::find_pattern(server_module,
                                          *switch_team_pattern));
    record_signature("CCSPlayerController_SwitchTeam", *switch_team_pattern,
                     impl_->switch_team != nullptr);

    impl_->set_pawn = reinterpret_cast<LuaCSGameApiImpl::SetPawnFn>(
        luacs_game_internal::find_pattern(server_module, *set_pawn_pattern));
    record_signature("CBasePlayerController_SetPawn", *set_pawn_pattern,
                     impl_->set_pawn != nullptr);

    impl_->event_manager_vtable =
        luacs_game_internal::find_virtual_table(server_module,
                                                "CGameEventManager");

    if (!missing_functions.empty()) {
        std::ostringstream message;
        message << "could not resolve required CS2 function signature(s): ";
        for (std::size_t index = 0; index < missing_functions.size(); ++index) {
            if (index != 0) message << ", ";
            message << missing_functions[index];
        }
        message << "\nSelected game server module: "
                << LuaCSGameServerModulePath().string();
        message << "\nSignature scan diagnostics:";
        for (const auto& detail : signature_diagnostics) {
            message << "\n  - " << detail;
        }
        message << "\nGamedata directory: "
                << (luacs_root / "gamedata" / "reference").string();
        error = message.str();
        return false;
    }

'@.Replace("`r`n", "`n")

$updated = $text.Substring(0, $start) + $replacement + $text.Substring($end)
foreach ($required in @(
    '#include "server_module.h"',
    'LuaCSGameServerModule()',
    'LuaCSGameServerModulePath().string()',
    'Selected game server module:',
    'Signature scan diagnostics:',
    'luacs_game_internal::g_pattern_scan_diagnostic',
    'pattern-bytes=',
    'Gamedata directory:',
    'record_signature("ClientPrint"',
    'record_signature("CBasePlayerController_SetPawn"'
)) {
    if (-not $updated.Contains($required)) {
        throw "generated deep signature diagnostics are missing '$required'"
    }
}
foreach ($obsolete in @(
    'GetModuleHandleW(L"server.dll")',
    'std::vector<const char*> missing_functions;'
)) {
    if ($updated.Contains($obsolete)) {
        throw "generated game API still contains obsolete token '$obsolete'"
    }
}

[System.IO.File]::WriteAllText(
    $resolvedPath,
    $updated,
    [System.Text.UTF8Encoding]::new($false))

Write-Host (
    'Injected bound-module selection and per-signature disk scan diagnostics ' +
    'into the generated game API.')
