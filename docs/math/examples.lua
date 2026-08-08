local math_api = require("cs2.math")

local a = Vector(0, 0, 0)
local b = Vector(300, 400, 0)

print("distance:", math_api.distance(a, b))
print("clamped:", math_api.clamp(125, 0, 100))
