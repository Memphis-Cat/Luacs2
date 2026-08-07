plugin = {
    name = "LuaCS Live API Test",
    author = "Memphis-Cat",
    version = "1.0.0",
    description = "Interactive live-server validation for the public LuaCS API."
}

local events = require("cs2.events")
local timers = require("cs2.timers")
local players = require("cs2.players")
local commands = require("cs2.commands")
local mathx = require("cs2.math")
local weapons = require("cs2.weapons")
local hud = require("cs2.hud")
local cvars = require("cs2.cvars")
local teams = require("cs2.teams")
local rounds = require("cs2.rounds")
local entities = require("cs2.entities")
local sounds = require("cs2.sounds")
local properties = require("cs2.properties")
local traces = require("cs2.traces")
local grenades = require("cs2.grenades")

local totals = { pass = 0, fail = 0, skip = 0 }
local event_counts = {}
local last_death = nil
local registered_events = {}

local function one_line(value)
    local text = tostring(value or "")
    text = text:gsub("[\r\n\t]", " ")
    return text
end

local function send(player, text)
    text = "[LuaTest] " .. one_line(text)
    print(text)
    if player then
        pcall(function() hud.console(player, text) end)
    end
end

local function record(kind, player, label, detail)
    totals[kind] = totals[kind] + 1
    local suffix = detail and detail ~= "" and (" - " .. one_line(detail)) or ""
    send(player, string.upper(kind) .. "  " .. label .. suffix)
end

local function pass(player, label, detail) record("pass", player, label, detail) end
local function fail(player, label, detail) record("fail", player, label, detail) end
local function skip(player, label, detail) record("skip", player, label, detail) end

local function check(player, label, fn)
    local ok, value, detail = pcall(fn)
    if not ok then
        fail(player, label, value)
        return false
    end
    if value == false or value == nil then
        fail(player, label, detail or "check returned false/nil")
        return false
    end
    pass(player, label, detail)
    return true
end

local function expect_error(player, label, fn)
    local ok = pcall(fn)
    if ok then
        fail(player, label, "operation unexpectedly succeeded")
        return false
    end
    pass(player, label, "invalid input was rejected safely")
    return true
end

local function words(text)
    local result = {}
    for word in tostring(text or ""):gmatch("%S+") do
        result[#result + 1] = word
    end
    return result
end

local function current_player(sender, args)
    if sender then return sender end
    local token = words(args)[2]
    if token then
        local slot = tonumber(token)
        if slot then return players.get_by_slot(slot) end
    end
    return nil
end

local function module_surface(player)
    local required = {
        {"events.on", events.on},
        {"events.on_post", events.on_post},
        {"timers.after", timers.after},
        {"timers.every", timers.every},
        {"players.all", players.all},
        {"commands.on", commands.on},
        {"math.distance", mathx.distance},
        {"weapons.active", weapons.active},
        {"hud.console", hud.console},
        {"cvars.get", cvars.get},
        {"teams.get_score", teams.get_score},
        {"rounds.state", rounds.state},
        {"entities.get", entities.get},
        {"sounds.emit", sounds.emit},
        {"properties.get", properties.get},
        {"traces.line", traces.line},
        {"grenades.list", grenades.list},
    }
    for _, item in ipairs(required) do
        if type(item[2]) == "function" then
            pass(player, item[1])
        else
            fail(player, item[1], "missing function")
        end
    end
end

local function player_tests(player)
    check(player, "players.all", function()
        local list = players.all()
        return type(list) == "table", "connected=" .. tostring(#list)
    end)

    expect_error(player, "players.get_by_slot rejects slot 999", function()
        players.get_by_slot(999)
    end)

    local target = current_player(player, "")
    if not target then
        skip(player, "live player fields", "run from an in-game player")
        return
    end

    check(player, "player.refresh", function()
        local refreshed, err = target:refresh()
        if not refreshed then return false, err end
        return target.valid == true,
            string.format("slot=%d health=%s armor=%s team=%s", target.slot,
                tostring(target.health), tostring(target.armor), tostring(target.team))
    end)
    check(player, "player.is_valid", function() return target:is_valid() == true end)
    check(player, "player identity", function()
        return type(target.name) == "string" and target.slot >= 0 and target.slot < 64,
            "name=" .. tostring(target.name) .. " steam64=" .. tostring(target.steam64)
    end)
end

local function weapon_tests(player, args)
    local target = current_player(player, args)
    if not target then
        skip(player, "weapon tests", "run from an in-game player")
        return
    end

    check(player, "weapons.list", function()
        local list, err = weapons.list(target)
        if not list then return false, err end
        return true, "count=" .. tostring(#list)
    end)

    local weapon, active_error = weapons.active(target)
    if not weapon then
        skip(player, "active weapon magazine", active_error or "no active weapon")
    else
        check(player, "weapon.refresh", function()
            local refreshed, err = weapon:refresh()
            if not refreshed then return false, err end
            return true, weapon.classname
        end)
        check(player, "magazine read clip1", function()
            return type(weapon.clip1) == "number", "clip1=" .. tostring(weapon.clip1)
        end)
        check(player, "reserve read reserve1", function()
            return type(weapon.reserve1) == "number", "reserve1=" .. tostring(weapon.reserve1)
        end)
        check(player, "magazine write/verify clip1", function()
            local before = weapon.clip1
            local ok, err = weapon:set_clip1(before)
            if not ok then return false, err end
            local refreshed, refresh_error = weapon:refresh()
            if not refreshed then return false, refresh_error end
            return weapon.clip1 == before,
                "preserved=" .. tostring(before)
        end)
        check(player, "reserve write/verify reserve1", function()
            local before = weapon.reserve1
            local ok, err = weapon:set_reserve1(before)
            if not ok then return false, err end
            local refreshed, refresh_error = weapon:refresh()
            if not refreshed then return false, refresh_error end
            return weapon.reserve1 == before,
                "preserved=" .. tostring(before)
        end)
    end

    local parsed = words(args)
    local ammo_type = tonumber(parsed[2] or "0") or 0
    check(player, "ammo-array read/write type " .. tostring(ammo_type), function()
        local before, err = weapons.get_ammo(target, ammo_type)
        if before == nil then return false, err end
        local ok, set_error = weapons.set_ammo(target, ammo_type, before)
        if not ok then return false, set_error end
        local after, read_error = weapons.get_ammo(target, ammo_type)
        if after == nil then return false, read_error end
        return after == before, "preserved=" .. tostring(before)
    end)
end

local function timer_tests(player)
    local started = timers.now()
    check(player, "timers.now finite", function()
        return type(started) == "number" and started >= 0, tostring(started)
    end)

    timers.after(0.05, function()
        local elapsed = timers.now() - started
        if elapsed >= 0 then
            pass(player, "timers.after callback", string.format("elapsed=%.4f", elapsed))
        else
            fail(player, "timers.after callback", "clock moved backwards")
        end
    end)

    local repeating
    repeating = timers.every(0.05, function()
        local cancelled = timers.cancel(repeating)
        if cancelled then
            pass(player, "timers.every self-cancel")
        else
            fail(player, "timers.every self-cancel", "timer cancel returned false")
        end
    end)
    pass(player, "timer callbacks scheduled", "two asynchronous results will follow")
end

local function cvar_tests(player)
    check(player, "cvars.exists sv_cheats", function()
        return cvars.exists("sv_cheats") == true
    end)
    check(player, "cvars.get sv_cheats", function()
        local value, err = cvars.get("sv_cheats")
        return value ~= nil, value or err
    end)
    check(player, "cvars.get_number mp_roundtime", function()
        local value, err = cvars.get_number("mp_roundtime")
        return value ~= nil, value or err
    end)
    check(player, "missing cvar returns nil/error", function()
        local value, err = cvars.get("luacs_this_cvar_should_not_exist")
        return value == nil and type(err) == "string", err
    end)
end

local function round_team_tests(player)
    check(player, "rounds.state", function()
        local state, err = rounds.state()
        if not state then return false, err end
        return state.valid == true,
            "round=" .. tostring(state.number) .. " frozen=" .. tostring(state.frozen)
    end)
    check(player, "teams.get_score T", function()
        local score, err = teams.get_score(teams.T)
        return score ~= nil, score or err
    end)
    check(player, "teams.get_score CT", function()
        local score, err = teams.get_score(teams.CT)
        return score ~= nil, score or err
    end)
    skip(player, "round/team mutations", "not run by safe test mode")
end

local function entity_tests(player)
    local target = current_player(player, "")
    if not target then
        skip(player, "entity tests", "run from an in-game player")
        return
    end
    local refreshed, err = target:refresh()
    if not refreshed or not target.has_pawn then
        skip(player, "entity tests", err or "player has no pawn")
        return
    end
    check(player, "entities.get player pawn", function()
        local entity, get_error = entities.get(target.pawn_index)
        if not entity then return false, get_error end
        return entity.valid == true,
            "index=" .. tostring(entity.entity_index) .. " class=" .. tostring(entity.classname)
    end)
    check(player, "entities.is_valid player pawn", function()
        return entities.is_valid(target.pawn_index) == true
    end)
    check(player, "entities.count_by_classname weapon_*", function()
        local count, count_error = entities.count_by_classname("weapon_*")
        return count ~= nil, count or count_error
    end)
end

local function property_tests(player)
    local target = current_player(player, "")
    if not target then
        skip(player, "property tests", "run from an in-game player")
        return
    end
    local refreshed, err = target:refresh()
    if not refreshed or not target.has_pawn then
        skip(player, "property tests", err or "player has no pawn")
        return
    end

    check(player, "properties.get_int m_iHealth", function()
        local value, get_error = properties.get_int(target.pawn_index, "m_iHealth")
        return value ~= nil, value or get_error
    end)
    check(player, "properties.info m_iHealth", function()
        local info, info_error = properties.info(target.pawn_index, "m_iHealth")
        if not info then return false, info_error end
        return info.valid == true and info.kind == "integer",
            "owner=" .. tostring(info.owner_class) .. " offset=" .. tostring(info.offset)
    end)
    check(player, "properties same-value write m_iHealth", function()
        local before, get_error = properties.get_int(target.pawn_index, "m_iHealth")
        if before == nil then return false, get_error end
        local ok, set_error = properties.set_int(target.pawn_index, "m_iHealth", before)
        if not ok then return false, set_error end
        local after, read_error = properties.get_int(target.pawn_index, "m_iHealth")
        if after == nil then return false, read_error end
        return after == before, "preserved=" .. tostring(before)
    end)
end

local function trace_tests(player)
    local target = current_player(player, "")
    if not target then
        skip(player, "trace tests", "run from an in-game player")
        return
    end
    local refreshed, err = target:refresh()
    if not refreshed or not target.position then
        skip(player, "trace tests", err or "position unavailable")
        return
    end
    local start_pos = {
        x = target.position.x,
        y = target.position.y,
        z = target.position.z + 16,
    }
    local end_pos = {
        x = start_pos.x,
        y = start_pos.y,
        z = start_pos.z + 128,
    }
    check(player, "traces.line", function()
        local result, trace_error = traces.line(start_pos, end_pos, { ignore = target })
        if not result then return false, trace_error end
        return result.valid == true,
            "hit=" .. tostring(result.hit) .. " fraction=" .. tostring(result.fraction)
    end)
end

local function grenade_tests(player)
    check(player, "grenades.count", function()
        local count, err = grenades.count()
        return count ~= nil, count or err
    end)
    check(player, "grenades.list", function()
        local list, err = grenades.list()
        if not list then return false, err end
        return type(list) == "table", "count=" .. tostring(#list)
    end)
    skip(player, "grenade spawn/detonate/remove", "not run by safe test mode")
end

local function data_type_tests(player)
    check(player, "Lua boolean", function() return type(true) == "boolean" end)
    check(player, "Lua integer", function() return math.type(42) == "integer" end)
    check(player, "Lua float", function() return math.type(42.5) == "float" end)
    check(player, "Lua string", function() return type("LuaCS") == "string" end)
    check(player, "Lua table", function() return type({}) == "table" end)

    local target = current_player(player, "")
    if target then
        check(player, "Steam64 exact type", function()
            local t = type(target.steam64)
            return t == "number" or t == "string", t .. ":" .. tostring(target.steam64)
        end)
    else
        skip(player, "Steam64 exact type", "run from an in-game player")
    end
end

local function event_status(player)
    local names = {}
    for name in pairs(event_counts) do names[#names + 1] = name end
    table.sort(names)
    if #names == 0 then
        skip(player, "event activity", "no watched events have fired yet")
        return
    end
    for _, name in ipairs(names) do
        pass(player, "event " .. name, "count=" .. tostring(event_counts[name]))
    end
end

local function death_status(player)
    if not last_death then
        skip(player, "player_death", "no death observed yet; kill a player and retry")
        return
    end
    pass(player, "player_death observed",
        string.format("victim=%s attacker=%s weapon=%s headshot=%s",
            last_death.victim, last_death.attacker, last_death.weapon,
            tostring(last_death.headshot)))
end

local function unsupported(player)
    skip(player, "Server.NextFrame", "public Lua API not implemented")
    skip(player, "Hooks.OnGameFrame", "public Lua API not implemented")
    skip(player, "arbitrary Source 2 function hooks", "public Lua API not implemented")
    skip(player, "ConVar change callbacks", "public Lua API not implemented")
    skip(player, "infinite-loop watchdog", "not implemented; do NOT run while true do end")
end

local function summary(player)
    send(player, string.format("SUMMARY pass=%d fail=%d skip=%d total=%d",
        totals.pass, totals.fail, totals.skip,
        totals.pass + totals.fail + totals.skip))
end

local function reset_results(player)
    totals.pass = 0
    totals.fail = 0
    totals.skip = 0
    event_counts = {}
    last_death = nil
    send(player, "results and event counters reset")
end

local categories = {}

categories.modules = function(player) module_surface(player) end
categories.players = function(player) player_tests(player) end
categories.weapons = function(player, args) weapon_tests(player, args) end
categories.ammo = function(player, args) weapon_tests(player, args) end
categories.timers = function(player) timer_tests(player) end
categories.cvars = function(player) cvar_tests(player) end
categories.rounds = function(player) round_team_tests(player) end
categories.teams = function(player) round_team_tests(player) end
categories.entities = function(player) entity_tests(player) end
categories.properties = function(player) property_tests(player) end
categories.traces = function(player) trace_tests(player) end
categories.grenades = function(player) grenade_tests(player) end
categories.types = function(player) data_type_tests(player) end
categories.events = function(player) event_status(player) end
categories.death = function(player) death_status(player) end
categories.unsupported = function(player) unsupported(player) end
categories.summary = function(player) summary(player) end
categories.reset = function(player) reset_results(player) end

local safe_all = {
    "modules", "players", "weapons", "timers", "cvars", "rounds",
    "entities", "properties", "traces", "grenades", "types",
    "events", "death", "unsupported"
}

local function show_help(player)
    send(player, "usage: !lua_test <category>  (or /lua_test <category>)")
    send(player, "categories: all modules players weapons ammo timers cvars rounds teams entities properties traces grenades types events death unsupported summary reset")
    send(player, "weapons/ammo optionally accept an ammo type: !lua_test ammo 2")
    send(player, "safe all does not kill/respawn players, change teams, restart rounds, spawn grenades, or emit sounds")
end

local function run_command(player, args)
    local parsed = words(args)
    local category = string.lower(parsed[1] or "help")
    if category == "help" or category == "" then
        show_help(player)
        return
    end
    if category == "all" then
        for _, name in ipairs(safe_all) do
            send(player, "--- " .. name .. " ---")
            categories[name](player, args)
        end
        summary(player)
        return
    end
    local fn = categories[category]
    if not fn then
        fail(player, "unknown category", category)
        show_help(player)
        return
    end
    fn(player, args)
    if category ~= "summary" and category ~= "reset" then summary(player) end
end

commands.on("lua_test", function(player, args, raw)
    local ok, err = pcall(run_command, player, args, raw)
    if not ok then
        fail(player, "lua_test command", err)
        summary(player)
    end
end)

commands.on("apitest", function(player, args, raw)
    local ok, err = pcall(run_command, player, args, raw)
    if not ok then
        fail(player, "apitest command", err)
        summary(player)
    end
end)

local watched_events = {
    "player_spawn", "player_hurt", "weapon_fire", "weapon_reload",
    "item_pickup", "round_start", "round_end", "round_freeze_end",
    "bomb_planted", "bomb_defused", "bomb_exploded", "bomb_beginplant",
    "bomb_abortplant", "bomb_begindefuse", "bomb_abortdefuse",
    "player_team", "player_jump", "player_blind", "grenade_thrown",
    "hegrenade_detonate", "flashbang_detonate", "smokegrenade_detonate",
    "molotov_detonate", "inferno_startburn", "inferno_expire"
}

for _, name in ipairs(watched_events) do
    local ok, id_or_error = pcall(function()
        return events.on_post(name, function()
            event_counts[name] = (event_counts[name] or 0) + 1
        end)
    end)
    if ok then
        registered_events[name] = id_or_error
    else
        print("[LuaTest] SKIP event registration " .. name .. " - " .. one_line(id_or_error))
    end
end

events.on_post("player_death", function(event)
    event_counts.player_death = (event_counts.player_death or 0) + 1
    local victim = event:get_player("userid")
    local attacker = event:get_player("attacker")
    last_death = {
        victim = victim and victim.name or "world/unknown",
        attacker = attacker and attacker.name or "world/unknown",
        weapon = event:get_string("weapon", "") or "",
        headshot = event:get_bool("headshot", false) == true,
    }
    print(string.format("[LuaTest] player_death victim=%s attacker=%s weapon=%s headshot=%s",
        last_death.victim, last_death.attacker, last_death.weapon,
        tostring(last_death.headshot)))
end)

events.on("map_start", function(event)
    event_counts.map_start = (event_counts.map_start or 0) + 1
    print("[LuaTest] map_start " .. tostring(event.map or ""))
end)

events.on("map_end", function()
    event_counts.map_end = (event_counts.map_end or 0) + 1
    print("[LuaTest] map_end")
end)

local lifecycle_names = {
    {"OnPlayerConnect", "player_connect"},
    {"OnPlayerActivate", "player_activate"},
    {"OnPlayerPutInServer", "player_put_in_server"},
    {"OnPlayerDisconnect", "player_disconnect"},
}
for _, item in ipairs(lifecycle_names) do
    local method = events.Instance[item[1]]
    if type(method) == "function" then
        method(events.Instance, function(player)
            event_counts[item[2]] = (event_counts[item[2]] or 0) + 1
            print("[LuaTest] " .. item[2] .. " " .. tostring(player and player.name or "nil"))
        end)
    end
end

function plugin:unload()
    print(string.format("[LuaTest] unload pass=%d fail=%d skip=%d",
        totals.pass, totals.fail, totals.skip))
end

print("[LuaTest] loaded. Use !lua_test help or /lua_test help in player chat.")
