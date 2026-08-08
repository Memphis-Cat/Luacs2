local players = require("cs2.players")
local commands = require("cs2.commands")

commands.on("whoami", function(player)
    if not player then return end

    local refreshed, err = player:refresh()
    if not refreshed then
        print("refresh failed:", err)
        return
    end

    print("slot:", player.slot)
    print("name:", player.name)
    print("steamid:", player.steamid)
    print("team:", player.team)
    print("health:", player.health)
    print("armor:", player.armor)
    print("position:", player.position)
end)
