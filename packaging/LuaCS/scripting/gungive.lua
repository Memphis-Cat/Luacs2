local events = require("cs2.events")
local weapons = require("cs2.weapons")

plugin = {
    name = "Gun Give",
    author = "LuaCS",
    version = "1.1.0",
    description = "Chat-event weapon-give diagnostic plugin."
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
            " was received from player_chat but userid could not be resolved")
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

local function trim(value)
    return (tostring(value or ""):match("^%s*(.-)%s*$") or "")
end

local function handle_chat(event)
    local text = event:get_string("text", "")
    if not text or text == "" then
        return
    end

    local prefix = text:sub(1, 1)
    if prefix ~= "!" and prefix ~= "/" then
        return
    end

    local body = trim(text:sub(2))
    local command_name, arguments = body:match("^(%S+)%s*(.-)$")
    command_name = string.lower(command_name or "")
    arguments = trim(arguments)
    if command_name == "" then
        return
    end

    local player = event:get_player("userid")
    print("[INFO] player_chat received command !" .. command_name .. " from " ..
        player_label(player) ..
        (arguments ~= "" and (" args='" .. arguments .. "'") or ""))

    local classname = aliases[command_name]
    if classname then
        give_weapon(player, classname, command_name)
        return
    end

    if command_name ~= "gun" then
        return
    end

    if arguments == "" then
        print("[WARN] !gun requires a classname, for example !gun weapon_ak47")
        return
    end

    local requested = string.lower(arguments)
    if not requested:match("^weapon_") then
        requested = "weapon_" .. requested
    end
    if not requested:match("^weapon_[%w_]+$") then
        print("[WARN] !gun rejected invalid classname: " .. requested)
        return
    end
    give_weapon(player, requested, "gun")
end

events.on("player_chat", handle_chat)

print("[INFO] Gun Give registered player_chat commands: !ak, !ak47, !awp, !deagle, !m4, !m4a1, !m4a1s, !gun <ak47|weapon_classname>")
