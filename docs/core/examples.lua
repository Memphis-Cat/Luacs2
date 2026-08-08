local cs2 = require("cs2")

local origin = Vector(100, 200, 64)
local tint = Color(255, 160, 32)

print("LuaCS core example")
print("origin:", origin)
print("tint:", tint)

local players = cs2.players
for _, player in ipairs(players.all()) do
    print(player.slot, player.name, player.steamid)
end
