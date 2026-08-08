# `cs2.events`

`cs2.events` handles two kinds of callbacks:

1. LuaCS lifecycle events such as player connection/map lifecycle callbacks.
2. Real Source 2 `IGameEvent` pre/post callbacks that can expose and, in pre hooks, modify the event.

```lua
local events = require("cs2.events")
```

## Register callbacks

```text
events.on(name, callback)
events.on_post(name, callback)
events.off(subscription_id)
```

Both registration functions return an exact subscription ID.

```lua
local id = events.on_post("player_death", function(event)
    print(event:get_string("weapon", ""))
end)

-- later
events.off(id)
```

## `player_death`

A common death listener:

```lua
events.on_post("player_death", function(event)
    local victim = event:get_player("userid")
    local attacker = event:get_player("attacker")
    local weapon = event:get_string("weapon", "")
    local headshot = event:get_bool("headshot", false)

    print("victim:", victim and victim.name or "unknown")
    print("attacker:", attacker and attacker.name or "world")
    print("weapon:", weapon)
    print("headshot:", headshot)
end)
```

## Event object fields

Game-event callback tables include runtime metadata such as event name/ID and whether the callback is pre or post. Mutable Source 2 events also carry an internal event token used by the methods below.

Lifecycle events do not have a mutable Source 2 token. Calling game-event getter/setter methods on a lifecycle event raises an error.

## Event getters

```text
event:has_key(key)
event:is_empty(key)
event:get_bool(key [, fallback=false])
event:get_int(key [, fallback=0])
event:get_uint64(key [, fallback=0])
event:get_float(key [, fallback=0])
event:get_string(key [, fallback=""])
event:get_player_slot(key)
event:get_entity_index(key)
event:get_pawn_index(key)
event:get_player(key)
```

`get_player` resolves the event's player slot and returns a LuaCS player table when that player is currently known.

Unsigned 64-bit event values are returned exactly. Large values become decimal strings rather than floats.

## Event setters

Pre-event callbacks can use:

```text
event:set_bool(key, value)
event:set_int(key, value)
event:set_uint64(key, value)
event:set_float(key, value)
event:set_string(key, value)
event:cancel()
event:set_dont_broadcast(boolean)
```

Setters return booleans. Strings cannot contain embedded NUL bytes. Boolean arguments are strict booleans. Integer and floating-point values are range/finite checked.

Post callbacks are best used for observing the result after Source 2 processed the event.

## `events.Instance`

LuaCS also provides dynamic CounterStrikeSharp-style names:

```lua
events.Instance.OnPlayerDeath(function(event)
    print("pre death", event)
end)

events.Instance.OnPostPlayerDeath(function(event)
    print("post death", event)
end)
```

The name after `On` or `OnPost` is converted from PascalCase to snake_case.

Connection lifecycle aliases such as `OnPlayerConnect`, `OnPlayerActivate`, `OnPlayerPutInServer`, and `OnPlayerDisconnect` receive the player table directly rather than a mutable game-event table.

## Useful event names

LuaCS does not hardcode a short whitelist; it registers by Source 2 event name. Common CS2 names include:

```text
player_connect
player_disconnect
player_spawn
player_death
player_hurt
weapon_fire
weapon_reload
round_start
round_end
round_freeze_end
bomb_planted
bomb_defused
bomb_exploded
grenade_thrown
smokegrenade_detonate
flashbang_detonate
hegrenade_detonate
molotov_detonate
decoy_detonate
```

Whether a particular event exists and which keys it contains is controlled by the current CS2 build.

## Callback failure isolation

An uncaught callback error disables only the plugin that owns that callback. Its event/command/timer registry references are released; neighboring plugins continue running.