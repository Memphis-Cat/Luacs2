plugin = {
    name = "LuaCS API Test Survivor",
    author = "Memphis-Cat",
    version = "1.0.0",
    description = "Proves a neighboring plugin survives another plugin being quarantined."
}

local commands = require("cs2.commands")
local events = require("cs2.events")
local timers = require("cs2.timers")

local spawn_count = 0
local timer_count = 0

commands.on("survivor", function(player, args, raw)
    print(string.format(
        "[LuaTestSurvivor] alive spawn_count=%d timer_count=%d sender=%s args=%s raw=%s",
        spawn_count, timer_count, tostring(player and player.name or "console"),
        tostring(args or ""), tostring(raw or "")))
end)

events.on_post("player_spawn", function()
    spawn_count = spawn_count + 1
end)

local timer
timer = timers.every(1.0, function()
    timer_count = timer_count + 1
    if timer_count >= 300 then
        timers.cancel(timer)
    end
end)

function plugin:unload()
    print(string.format("[LuaTestSurvivor] unload spawn_count=%d timer_count=%d",
        spawn_count, timer_count))
end

print("[LuaTestSurvivor] loaded. Use !survivor before and after the crash test.")
