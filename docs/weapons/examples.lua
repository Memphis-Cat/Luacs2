local weapons = require("cs2.weapons")
local commands = require("cs2.commands")

commands.on("ammo_info", function(player)
    if not player then return end

    local weapon = weapons.active(player)
    if not weapon then
        print("no active weapon")
        return
    end

    local refreshed, err = weapon:refresh()
    if not refreshed then
        print("weapon refresh failed:", err)
        return
    end

    print("weapon:", weapon.classname)
    print("slot:", weapon.slot)
    print("clip1:", weapon.clip1)
    print("reserve1:", weapon.reserve1)
end)

commands.on("give_awp", function(player)
    if not player then return end

    local weapon, err = weapons.replace_slot(player, "primary", "weapon_awp", true)
    if not weapon then
        print("replace failed:", err)
        return
    end

    print("equipped:", weapon.classname)
end)
