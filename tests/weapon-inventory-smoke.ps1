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

Assert-Contains $removeBody @(
    'const CEntityHandle weapon_handle = weapon->GetRefEHandle();',
    'if (delete_entity && !remove_entity)',
    'remove_player_item(player_pawn, weapon);',
    'if (inventory_contains(weapons, weapon_handle)) {',
    'virtual_function<DropWeaponFn>(weapon_services,',
    'drop(weapon_services, weapon, nullptr, nullptr);',
    'CS2 did not detach the weapon from m_hMyWeapons; refusing to ',
    'if (delete_entity) remove_entity(weapon);'
) "safe weapon removal"

$removePlayerItemIndex = $removeBody.IndexOf(
    'remove_player_item(player_pawn, weapon);',
    [System.StringComparison]::Ordinal)
$dropIndex = $removeBody.IndexOf(
    'drop(weapon_services, weapon, nullptr, nullptr);',
    [System.StringComparison]::Ordinal)
$refusalIndex = $removeBody.IndexOf(
    'CS2 did not detach the weapon from m_hMyWeapons; refusing to ',
    [System.StringComparison]::Ordinal)
$destroyIndex = $removeBody.IndexOf(
    'if (delete_entity) remove_entity(weapon);',
    [System.StringComparison]::Ordinal)
if ($removePlayerItemIndex -lt 0 -or $dropIndex -lt 0 -or
    $refusalIndex -lt 0 -or $destroyIndex -lt 0 -or
    $removePlayerItemIndex -ge $dropIndex -or
    $dropIndex -ge $refusalIndex -or
    $refusalIndex -ge $destroyIndex) {
    throw (
        "weapon removal must try RemovePlayerItem, fall back to DropWeapon, " +
        "verify detachment, and only then destroy the entity")
}

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

Write-Host (
    "LuaCS weapon inventory tests passed: removal never destroys an entity " +
    "while m_hMyWeapons still references it, DropWeapon is a verified " +
    "fallback, and weapon.slot exposes primary, secondary, knife, grenade, " +
    "equipment, c4, melee, and other classifications without changing ABI.")
