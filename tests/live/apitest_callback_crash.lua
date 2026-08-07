plugin = {
    name = "LuaCS API Test Callback Crash",
    author = "Memphis-Cat",
    version = "1.0.0",
    description = "Intentionally throws from a command callback to test per-plugin quarantine."
}

local commands = require("cs2.commands")
local events = require("cs2.events")
local timers = require("cs2.timers")

local should_never_run_after_crash = 0

commands.on("test_boom", function(player, args, raw)
    print("[LuaTestCrash] throwing intentional callback error now")
    error("INTENTIONAL LuaCS callback quarantine test")
end)

commands.on("boom_status", function()
    print("[LuaTestCrash] ERROR: quarantined plugin still accepted a command callback")
end)

events.on_post("player_spawn", function()
    should_never_run_after_crash = should_never_run_after_crash + 1
end)

timers.every(1.0, function()
    if should_never_run_after_crash > 0 then
        print("[LuaTestCrash] pre-crash activity=" .. tostring(should_never_run_after_crash))
    end
end)

function plugin:unload()
    print("[LuaTestCrash] unload called")
end

print("[LuaTestCrash] loaded. Run !test_boom exactly once, then verify !survivor still works.")
