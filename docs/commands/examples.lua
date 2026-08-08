local commands = require("cs2.commands")
local hud = require("cs2.hud")

commands.on("hello", function(player, arguments, raw)
    if not player then
        print("hello was called from the server console")
        return
    end

    local suffix = arguments ~= "" and (" " .. arguments) or ""
    hud.chat(player, "Hello " .. player.name .. suffix)
    print("command:", raw)
end)
