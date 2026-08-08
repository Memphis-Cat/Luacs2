# Core LuaCS values

LuaCS installs a small set of globals before a plugin starts. The rest of the API is loaded through `require`.

## `print(...)`

`print` accepts any Lua values. Values are converted to text, separated with tabs, written to the plugin log, and mirrored to the server console.

```lua
print("plugin started", 123, true)
```

A plugin has its own log file under `addons/LuaCS/logs`.

## `Vector([x, y, z])`

Creates a table with finite numeric `x`, `y`, and `z` fields. Missing arguments default to zero.

```lua
local origin = Vector(100, 200, 64)
local zero = Vector()

print(origin.x, origin.y, origin.z)
print(origin) -- Vector(100, 200, 64)
```

NaN and infinity are rejected.

Many LuaCS modules accept any table containing finite `x`, `y`, and `z` values, so you can build vectors yourself if needed. Using `Vector` keeps the format consistent.

## `Color(r, g, b [, a])`

Creates a color table. Every channel must be an integer from `0` to `255`. Alpha defaults to `255`.

```lua
local orange = Color(255, 128, 0)
local transparent = Color(255, 255, 255, 64)

print(orange) -- Color(255, 128, 0, 255)
```

## `require("cs2")`

The root `cs2` table loads module fields lazily.

```lua
local cs2 = require("cs2")
local players = cs2.players
local events = cs2.events
```

This is equivalent to loading modules directly:

```lua
local players = require("cs2.players")
local events = require("cs2.events")
```

Lua's normal `require` cache is used. Requiring the same module again returns the already-loaded module table for that plugin state.

## Globals loaded automatically

LuaCS loads these globals before your plugin source runs:

```text
cs2
players
events
timers
```

Other modules should be required explicitly so a plugin's dependencies are obvious.

## Plugin isolation

Each `.smg` package runs in its own Lua state. Globals, required-module tables, timers, event callbacks, command callbacks, and Lua registry references are not shared with another plugin.

An uncaught callback error disables that plugin's callbacks and timers. Other loaded plugins remain active. Fix the Lua error and refresh or reload the failed plugin from the server console.