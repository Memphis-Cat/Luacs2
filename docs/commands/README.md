# `cs2.commands`

`cs2.commands` registers Lua callbacks for commands routed through LuaCS.

```lua
local commands = require("cs2.commands")
```

## API

### `commands.on(name, callback)`

Registers a callback and returns the callback function.

```lua
commands.on("hello", function(player, arguments, raw)
    print(player and player.name or "console", arguments, raw)
end)
```

The callback receives:

```text
player     Player table for a player command, or nil for a server-console command
arguments  Everything after the command name
raw        Original command text passed to LuaCS
```

Chat commands beginning with `!` or `/` are routed from the Source 2 `player_chat` event. `say` and `say_team` are deliberately skipped in the `ClientCommand` hook so one chat message does not run the Lua callback twice.

For example, this registration:

```lua
commands.on("heal", function(player, arguments)
    if not player then return end
    player:set_health(100)
end)
```

can be triggered from chat with:

```text
!heal
/heal
```

## Command names

LuaCS normalizes command names for dispatch. Keep plugin command names simple: letters, numbers, and underscores are the safest choice.

## Errors

Registering a command requires a Lua function. If the runtime cannot store the callback, registration raises a Lua error.

If a command callback later throws an uncaught Lua error, LuaCS quarantines that plugin. Its callbacks and timers are released; other plugins keep running.