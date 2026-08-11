plugin = {
    name = "AWP Scout Roulette",
    author = "Byanca",
    version = "1.0.0",
    description = "Competitive AWP/Scout roulette with knife-only warmup and noscope rounds."
}

local events = require("cs2.events")
local timers = require("cs2.timers")
local players = require("cs2.players")
local weapons = require("cs2.weapons")
local entities = require("cs2.entities")
local properties = require("cs2.properties")

local WORKSHOP_ID = "3414483755"
local ATTACK2 = 0x800
local ZOOM = 0x400000000
local SCOPE_BUTTONS = ATTACK2 | ZOOM

local modes = {
    {
        name = "AWP Only",
        color = "#3F8CFF",
        weapon = "weapon_awp",
        noscope = false
    },
    {
        name = "AWP Only + Noscope",
        color = "#55D878",
        weapon = "weapon_awp",
        noscope = true
    },
    {
        name = "Scout Only",
        color = "#FF9B3D",
        weapon = "weapon_ssg08",
        noscope = false
    },
    {
        name = "Scout Only + Noscope",
        color = "#FFD83D",
        weapon = "weapon_ssg08",
        noscope = true
    }
}

local server_command_entity = nil
local workshop_requested = false
local warmup = true
local roulette_generation = 0
local pending_winner = nil
local current_mode = nil
local button_pointer_path_supported = nil

math.randomseed(os.time(), math.floor(timers.now() * 1000000))

local function ensure_server_command_entity()
    if server_command_entity and entities.is_valid(server_command_entity) then
        return server_command_entity
    end

    local entity = entities.create("point_servercommand", { spawn = true })
    if not entity then
        server_command_entity = nil
        return nil
    end

    server_command_entity = entity
    return entity
end

local function server_command(command)
    local entity = ensure_server_command_entity()
    if not entity then
        return nil
    end
    return entities.input(entity, "Command", command)
end

local function center_html(player, html, duration)
    if not player or player.slot == nil then
        return
    end

    local safe = html:gsub('"', '\\"')
    server_command(string.format(
        "luacs_centerhtml %d %d \"%s\"",
        player.slot,
        duration or 1,
        safe
    ))
end

local function center_mode_all(mode, duration)
    local html = string.format(
        "<font color='%s'><b>%s</b></font>",
        mode.color,
        mode.name
    )

    for _, player in ipairs(players.all()) do
        if player.connected and player.active then
            center_html(player, html, duration or 1)
        end
    end
end

local function set_zero_money(player)
    if not player then
        return
    end
    players.set_money(player, 0)
end

local function strip_to_knife(player)
    if not player then
        return
    end

    player = players.refresh(player) or player
    if not player.has_pawn then
        return
    end

    players.set_prevent_weapon_pickup(player, true)
    set_zero_money(player)

    weapons.remove_all(player)
    local knife = weapons.give(player, "weapon_knife")
    if knife then
        weapons.switch(player, knife)
    end
end

local function give_mode_weapon(player, mode)
    if not player or not mode then
        return
    end

    player = players.refresh(player) or player
    if not player.has_pawn then
        return
    end

    strip_to_knife(player)

    local rifle = weapons.give(player, mode.weapon)
    if rifle then
        weapons.switch(player, rifle)
    end
end

local function apply_current_loadout(player)
    if warmup or not current_mode then
        strip_to_knife(player)
        return
    end
    give_mode_weapon(player, current_mode)
end

local function for_each_live_player(callback)
    for _, player in ipairs(players.all()) do
        local refreshed = players.refresh(player)
        if refreshed and refreshed.has_pawn then
            callback(refreshed)
        end
    end
end

local function strip_everyone()
    for_each_live_player(strip_to_knife)
end

local function give_mode_everyone(mode)
    for_each_live_player(function(player)
        give_mode_weapon(player, mode)
    end)
end

local function clear_masked_uint(entity, path, mask)
    local value = properties.get_uint(entity, path)
    if type(value) ~= "number" then
        return false
    end

    local cleared = value & (~mask)
    if cleared ~= value then
        properties.set_uint(entity, path, cleared, nil, false)
    end
    return true
end

local function enforce_noscope_player(player)
    if not player or not player.has_pawn then
        return
    end

    if player.controller_index and player.controller_index >= 0 then
        local desired_fov = properties.get_uint(
            player.controller_index,
            "m_iDesiredFOV"
        )
        if type(desired_fov) == "number" and desired_fov ~= 0 then
            properties.set_uint(
                player.controller_index,
                "m_iDesiredFOV",
                0
            )
        end
    end

    local active = weapons.active(player)
    if active and
       (active.classname == "weapon_awp" or active.classname == "weapon_ssg08") then
        local zoom_level = properties.get_int(active, "m_zoomLevel")
        if type(zoom_level) == "number" and zoom_level ~= 0 then
            properties.set_int(active, "m_zoomLevel", 0)
        end
    end

    clear_masked_uint(
        player,
        "m_pMovementServices.m_nQueuedButtonDownMask",
        SCOPE_BUTTONS
    )
    clear_masked_uint(
        player,
        "m_pMovementServices.m_nQueuedButtonChangeMask",
        SCOPE_BUTTONS
    )
    clear_masked_uint(
        player,
        "m_pMovementServices.m_nButtonDoublePressed",
        SCOPE_BUTTONS
    )

    if button_pointer_path_supported ~= false then
        local ok = clear_masked_uint(
            player,
            "m_pMovementServices.m_nButtons.m_pButtonStates[0]",
            SCOPE_BUTTONS
        )
        button_pointer_path_supported = ok
    end
end

local function finish_roulette(generation, winner_index)
    if generation ~= roulette_generation or warmup then
        return
    end

    pending_winner = nil
    current_mode = modes[winner_index]
    center_mode_all(current_mode, 3)
    give_mode_everyone(current_mode)
end

local function roulette_step(generation, step, total_steps, winner_index)
    if generation ~= roulette_generation or warmup then
        return
    end

    local mode_index = ((step - 1) % #modes) + 1
    center_mode_all(modes[mode_index], 1)

    if step >= total_steps then
        finish_roulette(generation, winner_index)
        return
    end

    local progress = (step - 1) / math.max(1, total_steps - 1)
    local delay = 0.07 + (0.34 * progress * progress * progress)
    timers.after(delay, function()
        roulette_step(generation, step + 1, total_steps, winner_index)
    end)
end

local function start_roulette()
    if warmup then
        return
    end

    roulette_generation = roulette_generation + 1
    local generation = roulette_generation
    current_mode = nil

    strip_everyone()

    local winner_index = math.random(1, #modes)
    pending_winner = winner_index
    local total_steps = (#modes * 6) + winner_index

    timers.after(0.25, function()
        roulette_step(generation, 1, total_steps, winner_index)
    end)
end

local function configure_competitive()
    local commands = {
        "game_type 0",
        "game_mode 1",
        "exec gamemode_competitive",
        "mp_startmoney 0",
        "mp_maxmoney 0",
        "mp_buytime 0",
        "mp_buy_anywhere 0",
        "mp_buy_during_immunity 0",
        "mp_death_drop_gun 0",
        "mp_death_drop_grenade 0",
        "mp_death_drop_defuser 0",
        "mp_weapons_allow_map_placed 0",
        "mp_ct_default_primary \"\"",
        "mp_t_default_primary \"\"",
        "mp_ct_default_secondary \"\"",
        "mp_t_default_secondary \"\"",
        "mp_ct_default_melee weapon_knife",
        "mp_t_default_melee weapon_knife"
    }

    for _, command in ipairs(commands) do
        server_command(command)
    end
end

local function request_workshop_map()
    if workshop_requested then
        return
    end

    if not ensure_server_command_entity() then
        timers.after(1.0, request_workshop_map)
        return
    end

    workshop_requested = true
    server_command("host_workshop_map " .. WORKSHOP_ID)
end

events.on("map_start", function()
    server_command_entity = nil
    warmup = true
    current_mode = nil
    pending_winner = nil
    roulette_generation = roulette_generation + 1

    timers.after(0.25, function()
        configure_competitive()
        strip_everyone()
    end)
end)

events.on_post("round_announce_warmup", function()
    warmup = true
    current_mode = nil
    pending_winner = nil
    roulette_generation = roulette_generation + 1
    strip_everyone()
end)

events.on_post("round_announce_match_start", function()
    warmup = false
    current_mode = nil
    pending_winner = nil
    roulette_generation = roulette_generation + 1
    strip_everyone()
end)

events.on_post("round_start", function()
    if warmup then
        strip_everyone()
        return
    end
    start_roulette()
end)

events.on_post("round_freeze_end", function()
    if warmup then
        return
    end

    if not current_mode and pending_winner then
        finish_roulette(roulette_generation, pending_winner)
    elseif current_mode then
        give_mode_everyone(current_mode)
    end
end)

events.on_post("round_end", function()
    roulette_generation = roulette_generation + 1
    pending_winner = nil
    current_mode = nil
end)

events.on_post("player_spawn", function(event)
    local player = event:get_player("userid")
    if not player then
        return
    end

    timers.after(0.05, function()
        local refreshed = players.get_by_slot(player.slot)
        if refreshed then
            apply_current_loadout(refreshed)
        end
    end)
end)

events.on("game_frame", function()
    for _, player in ipairs(players.all()) do
        if player.connected then
            set_zero_money(player)
        end
    end

    if current_mode and current_mode.noscope then
        for_each_live_player(enforce_noscope_player)
    end
end)

request_workshop_map()
