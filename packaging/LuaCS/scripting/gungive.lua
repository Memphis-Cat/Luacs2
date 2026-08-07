local commands = require("cs2.commands")
local weapons = require("cs2.weapons")

plugin = {
    name = "Gun Give",
    author = "LuaCS",
    version = "1.0.0",
    description = "Chat commands for testing LuaCS weapon giving."
}

local aliases = {
    ak = "weapon_ak47",
    ak47 = "weapon_ak47",
    awp = "weapon_awp",
    deagle = "weapon_deagle",
    m4 = "weapon_m4a1",
    m4a1 = "weapon_m4a1",
    m4a1s = "weapon_m4a1_silencer"
}

local function player_label(player)
    if not player then
        return "unknown player"
    end
    return tostring(player.name or "player") .. " (slot " ..
        tostring(player.slot or "?") .. ")"
end

local function give_weapon(player, classname, command_name)
    if not player then
        print("[WARN] !" .. command_name ..
            " was invoked without a player context")
        return
    end

    local refreshed, refresh_error = player:refresh()
    if not refreshed then
        print("[WARN] !" .. command_name .. " refresh failed for " ..
            player_label(player) .. ": " .. tostring(refresh_error))
        return
    end

    if not player.has_pawn then
        print("[WARN] !" .. command_name .. " cannot give " .. classname ..
            " to " .. player_label(player) .. ": no live pawn")
        return
    end

    if player.alive == false then
        print("[WARN] !" .. command_name .. " cannot give " .. classname ..
            " to " .. player_label(player) .. ": player is dead")
        return
    end

    local weapon, give_error = weapons.give(player, classname)
    if not weapon then
        print("[WARN] !" .. command_name .. " failed to give " .. classname ..
            " to " .. player_label(player) .. ": " .. tostring(give_error))
        return
    end

    print("[INFO] !" .. command_name .. " gave " .. classname .. " to " ..
        player_label(player) .. " as entity " ..
        tostring(weapon.entity_index or "?"))
end

for command_name, classname in pairs(aliases) do
    commands.on(command_name, function(player)
        give_weapon(player, classname, command_name)
    end)
end

commands.on("gun", function(player, arguments)
    local requested = tostring(arguments or "")
    requested = requested:match("^%s*(.-)%s*$") or ""
    if requested == "" then
        print("[WARN] !gun requires a classname, for example !gun weapon_ak47")
        return
    end
    if not requested:match("^weapon_[%w_]+$") then
        print("[WARN] !gun rejected invalid classname: " .. requested)
        return
    end
    give_weapon(player, requested, "gun")
end)

print("[INFO] Gun Give registered chat commands: !ak, !ak47, !awp, !deagle, !m4, !m4a1, !m4a1s, !gun <weapon_classname>")
