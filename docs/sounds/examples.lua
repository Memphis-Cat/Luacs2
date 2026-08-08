local sounds = require("cs2.sounds")
local commands = require("cs2.commands")

commands.on("play_test_sound", function(player)
    if not player then return end

    local sound, err = sounds.emit(player, "sounds/example.vsnd_c", {
        source = player,
        volume = 1.0,
        pitch = 100,
        reliable = true,
    })

    if not sound then
        print("sound failed:", err)
        return
    end

    print("sound guid:", sound.guid)
end)
