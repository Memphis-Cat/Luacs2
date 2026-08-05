local events = require("cs2.events")
local commands = require("cs2.commands")
local players = require("cs2.players")

-- Generic event style: callback receives an event table.
events.on("player_connect", function(event)
    local player = event.player
    print("connected", player.name, player.steam64, player.steamid, player.steam3)
end)

-- Instance alias style: callback receives the player directly.
events.Instance:OnPlayerActivate(function(player)
    print("active", player.name, "slot", player.slot)
end)

commands.on("who", function(player)
    print(player and player.name or "console", "requested the connected player list")
    for _, connected in ipairs(players.all()) do
        print(connected.slot, connected.name, connected.steam64)
    end
end)
