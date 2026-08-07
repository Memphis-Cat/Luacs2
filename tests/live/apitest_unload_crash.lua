plugin = {
    name = "LuaCS API Test Unload Crash",
    author = "Memphis-Cat",
    version = "1.0.0",
    description = "Intentionally throws during unload to test unload failure handling."
}

local commands = require("cs2.commands")

commands.on("unload_crash_status", function()
    print("[LuaTestUnloadCrash] plugin is currently loaded")
end)

function plugin:unload()
    print("[LuaTestUnloadCrash] throwing intentional unload error now")
    error("INTENTIONAL LuaCS unload isolation test")
end

print("[LuaTestUnloadCrash] loaded. Use server console: lua plugins unload apitest_unload_crash")
