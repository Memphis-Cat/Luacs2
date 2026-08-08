# `cs2.grenades`

`cs2.grenades` exposes live CS2 grenade projectiles and inferno effects.

```lua
local grenades = require("cs2.grenades")
```

## Types

Constants:

```text
UNKNOWN
HE / HIGH_EXPLOSIVE
FLASHBANG
SMOKE
MOLOTOV
INCENDIARY
DECOY
INFERNO
```

Accepted string aliases include:

```text
he / hegrenade / high_explosive
flash / flashbang
smoke / smokegrenade
molotov
inc / incendiary / incgrenade
decoy
inferno / fire
```

For enumeration, `unknown`, `all`, and `*` mean all supported types.

Helpers:

```text
grenades.type_name(type)
grenades.type_id(type)
grenades.types
```

## Discovery

```text
grenades.get(entity_or_index)
grenades.is_valid(entity_or_index)
grenades.count([type])
grenades.list([type])
grenades.all([type])
grenades.by_owner(entity_or_player [, type])
grenades.by_thrower(player_or_slot [, type])
```

`by_owner` matches the owner entity or resolved thrower pawn. `by_thrower` filters by player slot.

## Spawn

Generic form:

```text
grenades.spawn(type, position, angles, velocity [, options])
grenades.create(...) -- alias of spawn
```

Example:

```lua
local grenade, err = grenades.spawn(
    "he",
    Vector(0, 0, 128),
    Vector(0, 0, 0),
    Vector(600, 0, 220),
    {
        thrower = player,
        fuse = 1.5,
        spawn = true,
    }
)
```

Typed helpers:

```text
spawn_he
spawn_flashbang
spawn_smoke
spawn_molotov
spawn_incendiary
spawn_decoy
spawn_inferno
```

## Spawn options

```text
owner
thrower
thrower_slot
fuse
spawn
```

`owner` can be an entity/index/player pawn.

`thrower` accepts a player object or slot.

If `thrower` and `thrower_slot` are both present, they must identify the same slot.

`fuse` is `-1` or a finite non-negative number of seconds. Inferno is already an active fire effect and does not support projectile fuse scheduling.

`spawn` is a strict boolean and defaults to true.

Position, angle, and velocity components must be finite.

Creation is transactional: failed initialization removes the newly-created entity.

## Grenade fields

```text
valid
type
type_id
classname
entity_index
handle
owner_entity_index
thrower_slot
thrower_entity_index
team
position
velocity
spawn_time
detonate_time
lifetime
exploded
smoke_active
bounce_count
bounce_sound
fire_count
smoke_effect_tick
projectile
effect
```

`projectile` is false for inferno. `effect` is true for inferno.

## Grenade methods

```text
grenade:refresh()
grenade:is_valid()
grenade:detonate()
grenade:remove()
grenade:fuse_duration()
```

Module forms:

```text
grenades.detonate(grenade)
grenades.remove(grenade)
```

`refresh` updates the same object in place. `remove` marks it invalid after native removal succeeds.

`fuse_duration()` returns `detonate_time - spawn_time` when the timing fields form a valid interval, otherwise `nil`.

Molotov and incendiary grenades share the engine's Molotov projectile class; LuaCS distinguishes incendiary state using the current schema field rather than inventing a separate class.

LuaCS checks grenade identity, entity existence, thrower slot, ownership, timing, classname, position, and velocity before returning native state to Lua.