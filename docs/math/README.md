# `cs2.math`

`cs2.math` contains small helpers used frequently by server plugins.

```lua
local math_api = require("cs2.math")
```

## API

### `math_api.distance(a, b)`

Returns the three-dimensional Euclidean distance between two vector tables.

```lua
local d = math_api.distance(Vector(0, 0, 0), Vector(3, 4, 0))
print(d) -- 5
```

Both arguments must be tables containing finite `x`, `y`, and `z` numbers. A result that overflows to a non-finite value is rejected.

### `math_api.clamp(value, minimum, maximum)`

Constrains `value` to the inclusive range `minimum..maximum`.

```lua
print(math_api.clamp(150, 0, 100)) -- 100
```

All three values must be finite. `minimum` cannot be greater than `maximum`.