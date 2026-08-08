local grenades = require("cs2.grenades")
local commands = require("cs2.commands")

commands.on("grenades", function()
    for _, grenade in ipairs(grenades.list()) do
        print(
            grenade.type,
            grenade.entity_index,
            grenade.thrower_slot,
            grenade.position
        )
    end
end)

commands.on("spawn_smoke", function(player)
    if not player then return end
    local refreshed, err = player:refresh()
    if not refreshed then
        print("player refresh failed:", err)
        return
    end

    local smoke, spawn_err = grenades.spawn_smoke(
        player.position,
        Vector(0, 0, 0),
        Vector(300, 0, 200),
        { thrower = player, fuse = 1.5 }
    )

    if not smoke then
        print("spawn failed:", spawn_err)
        return
    end

    print("spawned smoke entity:", smoke.entity_index)
end)
