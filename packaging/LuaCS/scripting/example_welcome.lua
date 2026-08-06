local events = require("cs2.events")
local commands = require("cs2.commands")
local players = require("cs2.players")
local weapons = require("cs2.weapons")
local hud = require("cs2.hud")
local cvars = require("cs2.cvars")

-- Generic event style: callback receives an event table.
events.on("player_connect", function(event)
    local player = event.player
    print("connected", player.name, player.steam64, player.steamid, player.steam3)
end)

-- Instance alias style: callback receives the player directly.
events.Instance:OnPlayerActivate(function(player)
    print("active", player.name, "slot", player.slot)
    hud.chat(player, "Welcome, " .. player.name .. "!")
end)

commands.on("who", function(player)
    print(player and player.name or "console", "requested the connected player list")
    for _, connected in ipairs(players.all()) do
        print(connected.slot, connected.name, connected.steam64)
    end
end)

-- In chat, use !ak or /ak. The exact CS2 classname is passed to the engine.
commands.on("ak", function(player)
    if not player then
        print("The ak command requires a player.")
        return
    end

    local ok, error_message = weapons.give(player, "weapon_ak47")
    if ok then
        hud.chat(player, "You received an AK-47.")
    else
        hud.chat(player, "Weapon error: " .. error_message)
    end
end)

commands.on("roundtime", function(player)
    local value, error_message = cvars.get_number("mp_roundtime")
    local message = value and ("mp_roundtime = " .. value)
        or ("Cvar error: " .. error_message)

    if player then
        hud.console(player, message)
    else
        print(message)
    end
end)
