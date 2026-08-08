local cvars = require("cs2.cvars")

local roundtime, err = cvars.get_number("mp_roundtime")
if not roundtime then
    print("could not read mp_roundtime:", err)
else
    print("mp_roundtime:", roundtime)
end

local cheats = cvars.get_bool("sv_cheats")
print("sv_cheats:", cheats)
