# LuaCS world APIs

This document covers the engine-facing LuaCS modules:

- `cs2.teams`
- `cs2.rounds`
- `cs2.entities`
- `cs2.sounds`
- `cs2.properties`
- `cs2.traces`
- `cs2.grenades`

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

## Traces

LuaCS uses the pinned CS2 SDK `Ray_t` constructors directly. All five Source 2 query shapes are exposed:

```lua
local traces = require("cs2.traces")

local line = traces.line(start_pos, end_pos, options)
local sphere = traces.sphere(start_pos, end_pos, radius, options)
local hull = traces.hull(start_pos, end_pos, mins, maxs, options)
local capsule = traces.capsule(
    start_pos, end_pos, center_a, center_b, radius, options)
local mesh = traces.mesh(
    start_pos, end_pos, mins, maxs, vertices, options)

local generic = traces.cast({
    shape = traces.SHAPE_CAPSULE,
    start = start_pos,
    ["end"] = end_pos,
    center_a = Vector(0, 0, -18),
    center_b = Vector(0, 0, 18),
    radius = 16,
})
```

Shape constants:

```text
SHAPE_LINE
SHAPE_SPHERE
SHAPE_HULL
SHAPE_CAPSULE
SHAPE_MESH
```

Options:

```text
mask / contents / interacts_with     64-bit interaction mask
interacts_as                         query interaction identity
interacts_exclude                    excluded interaction layers
collision_group                      0 through 255
object_set_mask                      Source 2 query-object set
hit_solid                            include solid shapes
hit_solid_requires_generate_contacts require solid-contact generation
hit_triggers                         include triggers
ignore_disabled_pairs                respect disabled collision pairs
ignore_if_both_hitboxes              reject matching hitbox-only pairs
force_hit_everything                 bypass normal interaction filtering
iterate_entities                     call entity-level filter logic
hit_entities                         include game entities; false is world-only
included_detail_layers               16-bit detail-layer mask
target_detail_layer                  0 through 255
ignore                               entity, entity index, or array
ignore_entities                      additional ignored entities
center / center_a / center_b          shape-local offsets
radius                               sphere/capsule radius
mins / maxs                          hull/mesh bounds
vertices                             mesh vertices
```

Up to `MAX_IGNORE_ENTITIES` (64) distinct entities and `MAX_MESH_VERTICES` (256) vertices are accepted. Every ignored index is validated; it is not silently dropped after Source 2's two built-in pass-entity slots. Additional ignores are enforced by `CTraceFilter::ShouldHitEntity`.

Results include the normal hit fields plus the stable Source 2 physics result data:

```text
valid, hit, start_solid, all_solid
fraction, distance, hit_offset
start, end, hit_position, plane_normal, plane_distance
entity_index, entity_handle, hitbox, hitgroup
surface_name, contents, contents64
triangle, bone, exact_hit_point, shape
physics_body, physics_shape
shape_interacts_as, shape_interacts_with, shape_interacts_exclude
shape_entity_id, shape_owner_id, shape_hierarchy_id
shape_detail_layer_mask, shape_detail_layer_mask_type
shape_target_detail_layer, shape_collision_group
shape_collision_function_mask
```

`fraction_left_solid_available` is `false` because Source 2 `CGameTrace` does not expose the Source 1 `fractionleftsolid` member. LuaCS keeps the legacy field at zero for ABI compatibility and explicitly marks it unavailable instead of inventing a value.

Every vector and scalar is checked for finite values. Degenerate hulls, capsules, meshes, zero/negative radii, invalid collision groups, invalid ignored entities, and oversized arrays return an error before calling the engine.

## Grenades

Supported grenade/effect types:

```text
HE / HIGH_EXPLOSIVE
FLASHBANG
SMOKE
MOLOTOV
INCENDIARY
DECOY
INFERNO
```

```lua
local grenades = require("cs2.grenades")

local projectile, err = grenades.spawn(
    grenades.HE,
    Vector(0, 0, 128),
    Vector(0, 0, 0),
    Vector(500, 0, 200),
    {
        owner = player.pawn_index,
        thrower_slot = player.slot,
        fuse = 1.5,
        spawn = true,
    })

local active = grenades.list()
local smoke = grenades.get(entity_index)
projectile:detonate()
projectile:remove()
```

Functions:

```text
get(entity_or_index)
list([type]) / all([type])
spawn(type, position, angles, velocity [, options])
detonate(entity_or_index)
remove(entity_or_index)
```

Returned objects expose:

```text
valid, type, type_id, classname
entity_index, handle
owner_entity_index
thrower_slot, thrower_entity_index
team
position, velocity
spawn_time, detonate_time, lifetime
exploded, smoke_active
bounce_count, bounce_sound
fire_count, smoke_effect_tick
```

Creation is transactional. If validation, teleporting, ownership, thrower assignment, spawning, fuse scheduling, or final inspection fails, the newly created entity is removed. A failed call cannot leave an orphan projectile behind.

Player slots are resolved by enumerating live player-controller entities and their pawn handles. The old assumption that controller entity index always equals `slot + 1` is used only as a validated compatibility fallback when the current schema does not expose a player-slot field.

Projectile fuse scheduling uses the real delayed `Detonate` entity input. Inferno is an active fire effect rather than a projectile: it supports inspection, enumeration, direct creation, extinguishing/removal, but rejects projectile fuse scheduling with an explicit error. Calling `detonate` on an inferno first attempts `Extinguish`, then removes it if the engine does not expose that input.

## Validation boundary

The Windows x64 CI build compiles all modules and the native Metamod plugin with MSVC warnings treated as errors (`/WX`). Package and compiler smoke tests verify every DLL is present and that SMG compilation, caching, corruption recovery, and syntax-failure preservation work.

CI cannot prove live CS2 behavior. Teams, rounds, entities, sounds, properties, traces, and grenades still require testing on a current Windows CS2 dedicated server with a loaded map. Engine operations return explicit errors when interfaces, schema fields, signatures, entities, recipients, game rules, ray inputs, or entity inputs are unavailable.
