plugin = {
    name = "Welcome Example",
    author = "Memphis-Cat",
    version = "1.0.0",
    description = "Demonstrates LuaCS events, players, weapons, HUD, and cvars."
}

function plugin:unload()
    print("Welcome Example is unloading cleanly.")
end

local events = require("cs2.events")
local commands = require("cs2.commands")
local players = require("cs2.players")
local weapons = require("cs2.weapons")
local hud = require("cs2.hud")
local cvars = require("cs2.cvars")
local timers = require("cs2.timers")

local SPAWN_RETRY_DELAY = 0.05
local MAX_SPAWN_ATTEMPTS = 20
local spawn_generation = {}

local function warn_operation(label, player, error_message)
    print("[WARN] " .. label .. " failed for slot " .. player.slot .. ": " ..
        tostring(error_message or "operation failed"))
end

local function apply_player_change(label, player, operation)
    local ok, error_message = operation()
    if not ok then
        warn_operation(label, player, error_message)
    end
    return ok
end

local function configure_spawn(player, generation, attempt)
    if spawn_generation[player.slot] ~= generation then return end

    local refreshed, refresh_error = player:refresh()
    if not refreshed or not player.has_pawn then
        if attempt < MAX_SPAWN_ATTEMPTS then
            timers.after(SPAWN_RETRY_DELAY, function()
                configure_spawn(player, generation, attempt + 1)
            end)
        else
            warn_operation("spawn setup", player,
                refresh_error or "player pawn did not become ready")
        end
        return
    end

    apply_player_change("set_health", player, function()
        return player:set_health(100)
    end)
    apply_player_change("set_armor", player, function()
        return player:set_armor(100)
    end)
    apply_player_change("set_money", player, function()
        return player:set_money(16000)
    end)
    apply_player_change("set_helmet", player, function()
        return player:set_helmet(true)
    end)

    local ak, give_error = weapons.give(player, "weapon_ak47")
    if not ak then
        warn_operation("weapon_ak47", player, give_error)
        return
    end

    local clip_ok, clip_error = ak:set_clip1(30)
    if not clip_ok then warn_operation("set_clip1", player, clip_error) end

    local reserve_ok, reserve_error = ak:set_reserve1(90)
    if not reserve_ok then
        warn_operation("set_reserve1", player, reserve_error)
    end

    hud.chat(player, "LuaCS live player and inventory API is active.")
end

-- LuaCS lifecycle event: player metadata is supplied directly.
events.Instance:OnPlayerActivate(function(player)
    print("active", player.name, "slot", player.slot)
    hud.chat(player, "Welcome, " .. player.name .. "!")
end)

-- Real CS2 game event, post-fire. CS2 can emit more than one early spawn event
-- while a player is joining, and the pawn may not exist until a later frame.
-- A generation token keeps only the newest setup request for each slot, while
-- bounded timer retries wait for the live pawn without hiding a real failure.
events.on_post("player_spawn", function(event)
    local player = event:get_player("userid")
    if not player then return end

    local generation = (spawn_generation[player.slot] or 0) + 1
    spawn_generation[player.slot] = generation
    timers.after(0.0, function()
        configure_spawn(player, generation, 1)
    end)
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
