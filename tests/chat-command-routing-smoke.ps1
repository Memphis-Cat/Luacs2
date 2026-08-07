$ErrorActionPreference = "Stop"

$root = (Resolve-Path "$PSScriptRoot\..").Path
$generator = Join-Path $root "tools\generate-server-module-plugin.ps1"
$pluginSource = Join-Path $root "src\plugin\plugin.cpp"
$gunGiveSource = Join-Path $root "packaging\LuaCS\scripting\gungive.lua"
$tempRoot = Join-Path ([System.IO.Path]::GetTempPath()) (
    "luacs-chat-routing-smoke-" + [Guid]::NewGuid().ToString("N"))
$generatedPlugin = Join-Path $tempRoot "plugin.cpp"

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

foreach ($requiredFile in @($generator, $pluginSource, $gunGiveSource)) {
    if (-not (Test-Path -LiteralPath $requiredFile -PathType Leaf)) {
        throw "required chat-routing test file is missing: $requiredFile"
    }
}

[System.IO.Directory]::CreateDirectory($tempRoot) | Out-Null
try {
    & powershell -NoProfile -ExecutionPolicy Bypass -File $generator `
        -Source $pluginSource -Destination $generatedPlugin
    if ($LASTEXITCODE -ne 0) {
        throw "server-module plugin generator failed with exit code $LASTEXITCODE"
    }

    $generatedText = [System.IO.File]::ReadAllText($generatedPlugin)
    Assert-Contains $generatedText @(
        'if (command_name == "say" || command_name == "say_team") return;',
        'std::string_view(event_name) == "player_chat"',
        'copy->GetString("text", "")',
        'copy->GetPlayerSlot("userid").Get()',
        "chat_text[0] == '!'",
        "chat_text[0] == '/'",
        'runtime_.client_command(player_slot, chat_text)',
        'runtime_.dispatch_game_event(token, event_name, copy->GetID()'
    ) "generated native chat bridge"

    $chatIndex = $generatedText.IndexOf(
        'std::string_view(event_name) == "player_chat"',
        [System.StringComparison]::Ordinal)
    $commandIndex = $generatedText.IndexOf(
        'runtime_.client_command(player_slot, chat_text)',
        [System.StringComparison]::Ordinal)
    $eventIndex = $generatedText.IndexOf(
        'runtime_.dispatch_game_event(token, event_name, copy->GetID()',
        [System.StringComparison]::Ordinal)
    if ($chatIndex -lt 0 -or $commandIndex -lt 0 -or $eventIndex -lt 0 -or
        $chatIndex -ge $commandIndex -or $commandIndex -ge $eventIndex) {
        throw "player_chat routing is not ordered before normal post-event dispatch"
    }

    $gunGiveText = [System.IO.File]::ReadAllText($gunGiveSource)
    Assert-Contains $gunGiveText @(
        'version = "1.2.0"',
        'events.on("player_chat", handle_chat)',
        'event:get_string("text", "")',
        'event:get_player("userid")',
        'requested = "weapon_" .. requested',
        'weapons.replace_slot(player, "auto", classname, true)',
        'tostring(weapon.slot)',
        'slot-replacing commands'
    ) "gungive player_chat replacement example"

    Assert-NotContains $gunGiveText @(
        'weapons.give(player, classname)',
        'primary_weapons',
        'secondary_weapons'
    ) "gungive replacement example"

    Write-Host (
        "LuaCS chat-command routing tests passed: Source 2 player_chat is the " +
        "authoritative chat path, ClientCommand skips say/say_team to prevent " +
        "duplicates, ! and / prefixes are bridged to commands.on, and the " +
        "gun example uses weapons.replace_slot in auto mode without duplicate " +
        "weapon-slot classification tables.")
}
finally {
    Remove-Item -LiteralPath $tempRoot -Recurse -Force -ErrorAction SilentlyContinue
}
