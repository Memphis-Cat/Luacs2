# `cs2.timers`

`cs2.timers` schedules Lua callbacks from the LuaCS game-frame clock.

```lua
local timers = require("cs2.timers")
```

## API

### `timers.after(delay, callback)`

Runs `callback` once after `delay` seconds and returns a timer ID.

```lua
timers.after(2.0, function()
    print("two seconds passed")
end)
```

### `timers.every(interval, callback)`

Runs `callback` repeatedly at the requested interval and returns a timer ID.

```lua
local id = timers.every(5.0, function()
    print("five-second tick")
end)
```

### `timers.cancel(id)`

Cancels a timer owned by the current plugin. Returns a boolean.

```lua
local stopped = timers.cancel(id)
```

### `timers.now()`

Returns LuaCS's monotonic runtime time in seconds.

```lua
local started = timers.now()
```

## Rules

Delays and intervals must be finite numbers and cannot be negative. Timer IDs are exact unsigned values; LuaCS keeps them exact even if an ID ever grows beyond the signed Lua integer range.

Timers belong to the plugin that created them. Unloading or quarantining the plugin releases its timer callbacks.

Timers run on the server thread. Do not put blocking loops, long file operations, or expensive work in a timer callback.