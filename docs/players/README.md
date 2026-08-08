# `cs2.players`

`cs2.players` resolves connected players and exposes live controller/pawn state.

```lua
local players = require("cs2.players")
```

Most functions accept either a player table or a slot number. Player slots are `0..63`.

## Lookup

### `players.get_by_slot(slot)`

Returns a player or `nil`.

### `players.get(value)`

Looks up a player by slot, Steam64 integer, exact player name, or Steam2 ID.

```lua
local by_slot = players.get(0)
local by_name = players.get("Player Name")
local by_steam2 = players.get("STEAM_1:0:12345")
```

### `players.all()`

Returns a Lua sequence of known connected players.

## Player fields

A refreshed player can contain:

```text
slot
name
steam64
steamid
fake
connected
active
valid
has_controller
has_pawn
alive
controller_index
pawn_index
pawn_handle
health
max_health
armor
team
money
ping
helmet
defuser
on_ground
position
velocity
eye_angles
```

`steam64` is exact. Values that do not fit a signed Lua integer are represented as an exact decimal string instead of a floating-point approximation.

Live fields require a valid Source 2 controller/pawn. A player can be connected while temporarily having no pawn.

## Refresh and state checks

```text
players.refresh(player_or_slot)
players.is_valid(player_or_slot)
players.is_alive(player_or_slot)
```

The same forms are methods:

```lua
player:refresh()
player:is_valid()
player:is_alive()
```

`refresh` updates an existing player table in place when a table is supplied.

## Health, armor, money, equipment

```text
players.set_health(player, value)
players.set_armor(player, value)
players.set_money(player, value)
players.set_helmet(player, boolean)
players.set_defuser(player, boolean)
players.set_prevent_weapon_pickup(player, boolean)
```

Method form:

```lua
player:set_health(100)
player:set_armor(100)
player:set_money(16000)
player:set_helmet(true)
player:set_defuser(true)
```

Boolean setters require actual Lua booleans. Integer mutations are checked before crossing the native boundary.

## Teleport

```lua
players.teleport(player, position, angles, velocity)
```

Any of the three transform arguments may be `nil`, but at least one must be provided.

```lua
player:teleport(
    Vector(100, 200, 64),
    Vector(0, 90, 0),
    Vector(0, 0, 0)
)
```

Every supplied vector component must be finite.

## Kill and respawn

```text
players.kill(player [, explode=false])
players.respawn(player)
```

Method form:

```lua
player:kill(false)
player:respawn()
```

`explode` is a strict boolean when supplied.

## Teams

```text
players.change_team(player, team)
players.switch_team(player, team)
```

`change_team` and `switch_team` call different Source 2 team-change paths. Both update the cached `player.team` field after success.

Constants:

```text
TEAM_NONE = 0
TEAM_SPECTATOR = 1
TEAM_T = 2
TEAM_CT = 3
```

For team score operations and team player lists, see [`../teams`](../teams/).

## Failure behavior

Live mutations usually return `true` or `nil, error_message`. `is_valid` and `is_alive` return booleans. A stale player table should be refreshed before relying on live pawn fields.