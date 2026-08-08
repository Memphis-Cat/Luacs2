# LuaCS documentation

This directory is the reference for LuaCS BETA TESTING 1.0.

The API is split by module so you can look up one area without reading a single large manual. Every module folder contains a `README.md` with the public API and an `examples.lua` file with working patterns.

## Start here

- [`core`](core/) - `Vector`, `Color`, `print`, `require("cs2")`, and module loading.
- [`plugins`](plugins/) - plugin metadata, load/unload behavior, errors, isolation, and server-console management.
- [`compiler`](compiler/) - compiling `.lua` files into authenticated `.smg` packages.

## Lua modules

- [`events`](events/) - lifecycle callbacks and mutable CS2 game events.
- [`timers`](timers/) - one-shot and repeating timers.
- [`players`](players/) - player lookup, live pawn/controller state, health, armor, money, teams, teleport, kill, and respawn.
- [`commands`](commands/) - chat and command callbacks.
- [`math`](math/) - LuaCS vector distance and clamp helpers.
- [`weapons`](weapons/) - inventory, active weapons, semantic slots, clips, reserve ammo, player ammo, drop, switch, remove, and replace.
- [`hud`](hud/) - chat, console, center, alert, and notify output.
- [`cvars`](cvars/) - Source 2 console variable reads and writes.
- [`teams`](teams/) - team membership and scores.
- [`rounds`](rounds/) - round state, restart, termination, freeze control, and round-end reason constants.
- [`entities`](entities/) - entity discovery, creation, spawn, removal, transforms, ownership, parenting, and inputs.
- [`sounds`](sounds/) - emit and stop Source 2 sounds with recipient filters.
- [`properties`](properties/) - Source 2 schema reflection and typed/raw property access.
- [`traces`](traces/) - line, sphere, hull, capsule, and mesh collision queries.
- [`grenades`](grenades/) - live grenade discovery, creation, filtering, detonation, and removal.

## Native design references

- [`ARCHITECTURE.md`](ARCHITECTURE.md) explains the runtime, module ABI, engine boundaries, plugin isolation, and generated Source 2 binding layer.
- [`SMG_FORMAT.md`](SMG_FORMAT.md) documents the authenticated `.smg` package format.

## Error convention

Lua argument mistakes normally raise a Lua error. Engine operations that can fail at runtime generally return `nil, error_message`. Boolean checks such as `is_valid` usually return `true` or `false` directly.

Do not ignore the error value when changing live engine state:

```lua
local ok, err = player:set_health(100)
if not ok then
    print("set_health failed:", err)
end
```

LuaCS rejects unsupported or unsafe operations instead of silently guessing a result.