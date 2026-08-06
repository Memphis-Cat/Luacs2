# LuaCS world APIs

This document covers the engine-facing modules added in LuaCS 0.4.0:

- `cs2.teams`
- `cs2.rounds`
- `cs2.entities`
- `cs2.sounds`

All mutating operations return `true` or a result object on success. Failures return `nil, error_message`; unsupported engine behavior is not simulated.

## Teams

```lua
local teams = require("cs2.teams")

teams.change(player, teams.CT)
teams.switch(player, teams.T)

local terrorists = teams.get_players(teams.T)
local score = teams.get_score(teams.CT)
teams.set_score(teams.CT, 5)
local new_score = teams.add_score(teams.T, 1)
```

Constants:

```text
NONE = 0
SPECTATOR = 1
T = 2
TERRORIST = 2
CT = 3
COUNTER_TERRORIST = 3
```

`change` follows normal CS2 team-change behavior. `switch` uses the forced live switch exposed by the existing player API. Team scores are read and written through the live `CTeam::m_iScore` schema field and are network-state marked after mutation.

## Rounds

```lua
local rounds = require("cs2.rounds")

local state = rounds.state()
print(state.number, state.frozen, state.win_status, state.win_reason)

rounds.restart(1.0)
rounds.terminate(rounds.CT_WIN, 0.0)
rounds.freeze()
rounds.unfreeze()
```

Functions:

```text
state()
get()
get_number()
is_frozen()
restart([delay_seconds=1.0])
terminate(reason [, delay_seconds=0.0])
freeze()
unfreeze()
```

Round termination constants include the current CS2 round-end reasons:

```text
UNKNOWN
TARGET_BOMBED
TERRORISTS_ESCAPED
CTS_PREVENT_ESCAPE
ESCAPING_TERRORISTS_NEUTRALIZED
BOMB_DEFUSED
CT_WIN / CTS_WIN
T_WIN / TERRORISTS_WIN
DRAW / ROUND_DRAW
ALL_HOSTAGES_RESCUED
TARGET_SAVED
HOSTAGES_NOT_RESCUED
TERRORISTS_NOT_ESCAPED
GAME_COMMENCING
TERRORISTS_SURRENDER
CTS_SURRENDER
TERRORISTS_PLANTED
CTS_REACHED_HOSTAGE
SURVIVAL_WIN
SURVIVAL_DRAW
```

`restart` writes the real `mp_restartgame` cvar. `terminate` calls the verified Windows `CCSGameRules_TerminateRound` function. Freeze state and round metadata use live `CCSGameRules` schema fields.

## Entities

```lua
local entities = require("cs2.entities")

local weapons = entities.find_by_classname("weapon_*")
local door = entities.find_by_name("door_1")
local entity = entities.get(123)

local prop, err = entities.create("prop_dynamic", {
    position = Vector(100, 200, 300),
    angles = Vector(0, 90, 0),
    velocity = Vector(0, 0, 0),
    spawn = true,
})

if prop then
    prop:set_position(Vector(150, 200, 300))
    prop:set_angles(Vector(0, 180, 0))
    prop:set_velocity(Vector(0, 0, 100))
    prop:set_owner(player.pawn_index)
    prop:set_parent(door)
    prop:input("Enable")
    prop:remove()
end
```

Discovery:

```text
get(entity_or_index)
all()
find_by_classname(pattern)
find_by_name(pattern)          -- first match
find_all_by_name(pattern)
count_by_classname(pattern)
count_by_name(pattern)
is_valid(entity_or_index)
```

Classname and target-name patterns support `*` and `?` wildcards and are matched case-insensitively.

Lifecycle and mutation:

```text
create(classname [, options])
spawn(entity_or_index)
remove(entity_or_index)
refresh(entity_or_index)
teleport(entity_or_index [, position] [, angles] [, velocity])
set_position(entity_or_index, vector)
set_angles(entity_or_index, vector)
set_velocity(entity_or_index, vector)
set_owner(entity_or_index [, owner])
set_parent(entity_or_index [, parent])
input(entity_or_index, input_name [, value] [, activator] [, caller] [, delay])
```

Entity objects expose:

```text
valid
spawned
entity_index
handle
classname
name
health
team
position
angles
velocity
owner_index
owner
parent_index
parent
```

Objects also expose the mutation functions as methods.

Creation uses `UTIL_CreateEntityByName`; spawning uses `CBaseEntity_DispatchSpawn`; removal uses `UTIL_Remove`; transforms use the live `CBaseEntity::Teleport` virtual; inputs use `CEntityInstance_AcceptInput` or the delayed entity-I/O queue. Owner and parent values are resolved from live entity handles and scene nodes.

An entity table is a snapshot. Call `entity:refresh()` before relying on values that may have changed. Removing an entity invalidates that table; stale indexes are rejected by the engine bridge.

## Sounds

```lua
local sounds = require("cs2.sounds")

local sound, err = sounds.emit(player, "sounds/example.vsnd", {
    volume = 0.8,
    pitch = 100,
    channel = 4,
    origin = Vector(100, 200, 300),
    source = player,
    reliable = true,
})

if sound then
    sound:stop()
end

sounds.emit_all("sounds/example.vsnd", {
    volume = 1.0,
    pitch = 95,
    channel = 7,
})

sounds.stop_channel(7)
```

Functions:

```text
emit(recipient_or_recipients, sound_name [, options])
emit_to(recipient_or_recipients, sound_name [, options])
emit_all(sound_name [, options])
stop(sound_or_guid [, recipients] [, reliable=true])
stop_channel(channel [, recipients] [, reliable=true])
```

Recipients can be a player object, slot, or an array of players/slots. `emit_all` targets every connected player.

Options:

```text
source      entity, player, entity index, or pawn index
origin      Vector
volume      0.0 through 10.0
pitch       1 through 255
delay       0.0 through 3600.0 seconds
channel     LuaCS logical channel integer
reliable    boolean
```

A returned sound object exposes:

```text
valid
guid
stack_hash
source_entity_index
recipients_mask
channel
name
```

Emission calls the real CS2 sound-event path and stores the returned SOS GUID. Stopping sends `CMsgSosStopSoundEvent` to the selected recipients. Channels are LuaCS logical groups used to stop one or more tracked GUIDs; they are not presented as a fabricated Valve mixer-channel API.

## Validation boundary

The Windows x64 CI build compiles all modules and the native Metamod plugin with MSVC warnings treated as errors. Package and compiler smoke tests verify every DLL is present and that SMG compilation, caching, corruption recovery, and syntax-failure preservation work.

CI cannot prove live CS2 behavior. Teams, rounds, entities, and sounds still require testing on a current Windows CS2 dedicated server with a loaded map. Engine operations return explicit errors when interfaces, schema fields, signatures, entities, recipients, or game rules are unavailable.
