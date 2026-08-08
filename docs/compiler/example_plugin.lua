local commands = require("cs2.commands")
local hud = require("cs2.hud")

plugin = {
    name = "Compiler Example",
    author = "Byanca",
    version = "1.0.0",
    description = "Small plugin used as a compile.exe example."
}

commands.on("compiler_example", function(player)
    if player then
        hud.chat(player, "Compiler example is loaded.")
    end
end)
