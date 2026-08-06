local events = require("cs2.events")
local commands = require("cs2.commands")
local players = require("cs2.players")
local weapons = require("cs2.weapons")
local hud = require("cs2.hud")
local cvars = require("cs2.cvars")

-- LuaCS lifecycle event: player metadata is supplied directly.
events.Instance:OnPlayerActivate(function(player)
    print("active", player.name, "slot", player.slot)
    hud.chat(player, "Welcome, " .. player.name .. "!")
end)

-- Real CS2 game event, pre-fire. The event object is live and mutable only
-- during this callback. Typed getters resolve player/entity fields safely.
events.on("player_spawn", function(event)
    local player = event:get_player("userid")
    if not player then return end

    player:set_health(100)
    player:set_armor(100)
    player:set_money(16000)
    player:set_helmet(true)

    local ak, give_error = weapons.give(player, "weapon_ak47")
    if not ak then
        print("[WARN] weapon_ak47 failed:", give_error)
        return
    end

    ak:set_clip1(30)
    ak:set_reserve1(90)
    hud.chat(player, "LuaCS live player and inventory API is active.")
end)

-- Read fields from a real CS2 event.
events.on("player_hurt", function(event)
    local victim = event:get_player("userid")
    if victim then
        print(victim.name, "health after hit:", event:get_int("health"))
    end
end)

-- Pre/post subscriptions are separate. Pre callbacks may mutate/cancel;
-- post callbacks receive a safe duplicate after CS2 fires the event.
events.on("round_start", function(event)
    print("round_start pre", event.id, "reliable", event.reliable)
end)

events.on_post("round_start", function(event)
    print("round_start post", event.id)
end)

commands.on("who", function(player)
    print(player and player.name or "console", "requested the player list")
    for _, connected in ipairs(players.all()) do
        connected:refresh()
        print(connected.slot, connected.name, connected.health,
              connected.armor, connected.money, connected.team)
    end
end)

commands.on("inventory", function(player)
    if not player then
        print("The inventory command requires a player.")
        return
    end

    for _, weapon in ipairs(weapons.list(player)) do
        hud.console(player,
            string.format("%s entity=%d clip=%d reserve=%d active=%s",
                weapon.classname, weapon.entity_index, weapon.clip1,
                weapon.reserve1, tostring(weapon.active)))
    end
end)

commands.on("ak", function(player)
    if not player then
        print("The ak command requires a player.")
        return
    end

    local weapon, error_message = weapons.give(player, "weapon_ak47")
    if not weapon then
        hud.chat(player, "Weapon error: " .. error_message)
        return
    end

    weapon:set_clip1(30)
    weapon:set_reserve1(90)
    weapon:switch()
    hud.chat(player, "You received and equipped an AK-47.")
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
