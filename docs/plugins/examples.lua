local commands = require("cs2.commands")
local timers = require("cs2.timers")

plugin = {
    name = "Plugin Lifecycle Example",
    author = "Byanca",
    version = "1.0.0",
    description = "Shows metadata, commands, timers, and cleanup."
}

local timer_id = timers.every(30, function()
    print(plugin.name, "is still running")
end)

commands.on("plugin_name", function(player)
    print("requested by", player and player.name or "server console")
end)

function OnUnload()
    timers.cancel(timer_id)
    print(plugin.name, "unloaded")
end
