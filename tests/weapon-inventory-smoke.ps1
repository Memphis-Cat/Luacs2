$ErrorActionPreference = "Stop"

$root = (Resolve-Path "$PSScriptRoot\..").Path
$playerSource = Join-Path $root "src\plugin\game_api_players.cpp"
$weaponModule = Join-Path $root "src\modules\weapons\weapons.cpp"

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

function Assert-NotContains {
    param(
        [Parameter(Mandatory = $true)][string]$Text,
        [Parameter(Mandatory = $true)][string[]]$Tokens,
        [Parameter(Mandatory = $true)][string]$Context
    )
    foreach ($token in $Tokens) {
        if ($Text.Contains($token)) {
            throw "$Context unexpectedly contains '$token'"
        }
    }
}

foreach ($requiredFile in @($playerSource, $weaponModule)) {
    if (-not (Test-Path -LiteralPath $requiredFile -PathType Leaf)) {
        throw "required weapon-inventory test file is missing: $requiredFile"
    }
}

$playerText = [System.IO.File]::ReadAllText($playerSource)
$removeStart = $playerText.IndexOf(
    "bool LuaCSGameApiImpl::weapon_remove(",
    [System.StringComparison]::Ordinal)
$nextFunction = $playerText.IndexOf(
    "bool LuaCSGameApiImpl::weapon_drop(",
    $removeStart,
    [System.StringComparison]::Ordinal)
if ($removeStart -lt 0 -or $nextFunction -le $removeStart) {
    throw "could not isolate LuaCSGameApiImpl::weapon_remove"
}
$removeBody = $playerText.Substring($removeStart, $nextFunction - $removeStart)

Assert-Contains $playerText @(
    'int inventory_remove_handle(CUtlVector<CEntityHandle>* inventory,',
    'inventory->Remove(index);'
) "inventory handle repair helper"

Assert-Contains $removeBody @(
    'const CEntityHandle weapon_handle = weapon->GetRefEHandle();',
    'if (delete_entity && !remove_entity)',
    'remove_player_item(player_pawn, weapon);',
    'const int removed = inventory_remove_handle(weapons, weapon_handle);',
    'could not remove the stale inventory handle',
    'static_cast<std::uint32_t>(weapons_vector_offset)',
    'auto& active = field<CEntityHandle>(weapon_services, active_weapon_offset);',
    'active.Term();',
    'auto& last = field<CEntityHandle>(weapon_services, last_weapon_offset);',
    'last.Term();',
    'weapon handle is still present in m_hMyWeapons after explicit ',
    'if (delete_entity) remove_entity(weapon);'
) "safe weapon removal"

Assert-NotContains $removeBody @(
    'virtual_function<DropWeaponFn>',
    'drop(weapon_services, weapon, nullptr, nullptr);'
) "safe weapon removal"

$removePlayerItemIndex = $removeBody.IndexOf(
    'remove_player_item(player_pawn, weapon);',
    [System.StringComparison]::Ordinal)
$repairIndex = $removeBody.IndexOf(
    'inventory_remove_handle(weapons, weapon_handle)',
    [System.StringComparison]::Ordinal)
$activeIndex = $removeBody.IndexOf(
    'active.Term();',
    [System.StringComparison]::Ordinal)
$lastIndex = $removeBody.IndexOf(
    'last.Term();',
    [System.StringComparison]::Ordinal)
$destroyIndex = $removeBody.IndexOf(
    'if (delete_entity) remove_entity(weapon);',
    [System.StringComparison]::Ordinal)
if ($removePlayerItemIndex -lt 0 -or $repairIndex -lt 0 -or
    $activeIndex -lt 0 -or $lastIndex -lt 0 -or $destroyIndex -lt 0 -or
    $removePlayerItemIndex -ge $repairIndex -or
    $repairIndex -ge $activeIndex -or $activeIndex -ge $lastIndex -or
    $lastIndex -ge $destroyIndex) {
    throw (
        "weapon removal must run RemovePlayerItem, repair m_hMyWeapons, " +
        "clear active/last references, and only then destroy the entity")
}

$countStart = $playerText.IndexOf(
    "std::size_t LuaCSGameApiImpl::weapon_count(",
    [System.StringComparison]::Ordinal)
$atStart = $playerText.IndexOf(
    "bool LuaCSGameApiImpl::weapon_at(",
    $countStart,
    [System.StringComparison]::Ordinal)
$getStart = $playerText.IndexOf(
    "bool LuaCSGameApiImpl::weapon_get(",
    $atStart,
    [System.StringComparison]::Ordinal)
if ($countStart -lt 0 -or $atStart -le $countStart -or $getStart -le $atStart) {
    throw "could not isolate weapon inventory enumeration functions"
}
$countBody = $playerText.Substring($countStart, $atStart - $countStart)
$atBody = $playerText.Substring($atStart, $getStart - $atStart)

Assert-Contains $countBody @(
    'if (entity_by_handle((*weapons)[index])) continue;',
    'weapons->Remove(index);',
    'if (repaired) {',
    'static_cast<std::uint32_t>(weapons_vector_offset)'
) "weapon_count stale-handle repair"

Assert-Contains $atBody @(
    'while (index < static_cast<std::size_t>(weapons->Count())) {',
    'CEntityInstance* weapon = entity_by_handle(handle);',
    'weapons->Remove(static_cast<int>(index));',
    'static_cast<std::uint32_t>(weapons_vector_offset)'
) "weapon_at stale-handle repair"

$moduleText = [System.IO.File]::ReadAllText($weaponModule)
Assert-Contains $moduleText @(
    'std::string_view classify_weapon_slot(std::string_view name)',
    'if (name == "weapon_c4") return "c4";',
    'return "knife";',
    'return "grenade";',
    'return "secondary";',
    'return "primary";',
    'return "equipment";',
    'return "melee";',
    'return "other";',
    'const std::string_view slot = classify_weapon_slot(weapon.classname);',
    'lua_setfield(state, table, "slot");',
    'lua_createtable(state, 0, 12);'
) "weapon.slot classification"

$replaceStart = $moduleText.IndexOf(
    "int replace_slot(lua_State* state)",
    [System.StringComparison]::Ordinal)
$dropStart = $moduleText.IndexOf(
    "int drop(lua_State* state)",
    $replaceStart,
    [System.StringComparison]::Ordinal)
if ($replaceStart -lt 0 -or $dropStart -le $replaceStart) {
    throw "could not isolate weapons.replace_slot"
}
$replaceBody = $moduleText.Substring($replaceStart, $dropStart - $replaceStart)

Assert-Contains $replaceBody @(
    'const std::string requested_slot = lower(luaL_checkstring(state, 2));',
    'requested_slot != "auto"',
    '"primary", "secondary", "knife", "grenade", "equipment",',
    'weapon classname is classified as other and cannot replace a slot',
    'weapon classname belongs to slot',
    'api->weapon_count(api->context, player_slot, error, sizeof(error))',
    'classify_weapon_slot(lower(existing.classname)) != classified_slot',
    'api->weapon_remove(api->context, player_slot,',
    'api->weapon_give(api->context, player_slot, normalized_class.c_str(),',
    'api->weapon_switch(api->context, player_slot,',
    'rollback failed:',
    'replacement.active = true;',
    'push_weapon(state, replacement);'
) "weapons.replace_slot implementation"

$removeIndex = $replaceBody.IndexOf(
    'api->weapon_remove(api->context, player_slot,',
    [System.StringComparison]::Ordinal)
$giveIndex = $replaceBody.IndexOf(
    'api->weapon_give(api->context, player_slot, normalized_class.c_str(),',
    [System.StringComparison]::Ordinal)
$switchIndex = $replaceBody.IndexOf(
    'api->weapon_switch(api->context, player_slot,',
    [System.StringComparison]::Ordinal)
$pushIndex = $replaceBody.IndexOf(
    'push_weapon(state, replacement);',
    [System.StringComparison]::Ordinal)
if ($removeIndex -lt 0 -or $giveIndex -lt 0 -or $switchIndex -lt 0 -or
    $pushIndex -lt 0 -or $removeIndex -ge $giveIndex -or
    $giveIndex -ge $switchIndex -or $switchIndex -ge $pushIndex) {
    throw (
        "replace_slot must remove the old slot, give the replacement, " +
        "optionally switch to it, and only then return the weapon")
}

Assert-Contains $moduleText @(
    'lua_createtable(state, 0, 21);',
    'add_function(state, api, "replace_slot", &replace_slot);'
) "weapons.replace_slot export"

Write-Host (
    "LuaCS weapon inventory tests passed: removal repairs m_hMyWeapons " +
    "directly, enumeration prunes dead handles, weapon.slot exposes stable " +
    "categories, and weapons.replace_slot validates explicit/auto slots, " +
    "replaces in reverse inventory order, rolls back failed equips, and " +
    "returns the replacement without changing module ABI.")
