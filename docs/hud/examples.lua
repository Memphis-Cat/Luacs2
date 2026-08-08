local hud = require("cs2.hud")
local commands = require("cs2.commands")

commands.on("messages", function(player)
    if not player then return end

    hud.chat(player, "Chat message")
    hud.center(player, "Center message")
    hud.console(player, "Console message")
end)
