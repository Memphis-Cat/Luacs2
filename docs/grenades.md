# LuaCS grenades

`cs2.grenades` exposes live CS2 grenade projectiles and inferno fire effects.
The native implementation uses the current Source 2 schema fields for grenade
identity, thrower/owner state, projectile initialization, fuse scheduling, and
detonation. Creation remains transactional: failed setup removes the newly
created entity instead of leaving an orphan behind.

The AdvancedWorld ABI remains v3.

## Supported types

```text
HE / HIGH_EXPLOSIVE
FLASHBANG
SMOKE
MOLOTOV
INCENDIARY
DECOY
INFERNO
```

The main functions accept either numeric constants or string aliases.
Examples:

```lua
grenades.list("smoke")
grenades.count("he")
grenades.spawn("molotov", position, angles, velocity)
```

Accepted string names include:

```text
he / hegrenade / high_explosive
flash / flashbang
smoke / smokegrenade
molotov
inc / incendiary / incgrenade
decoy
inferno / fire
```

For enumeration only, `unknown`, `all`, and `*` mean all supported types.

`grenades.types` maps canonical lowercase names to numeric type IDs.

Helpers:

```lua
grenades.type_name(grenades.HE)     -- "he"
grenades.type_id("smoke")           -- grenades.SMOKE
```

## Discovery

```text
get(entity_or_index)
is_valid(entity_or_index)
count([type])
list([type])
all([type])
by_owner(entity_or_player [, type])
by_thrower(player_or_slot [, type])
```

`get` only succeeds for supported grenade projectile/inferno entities.
`is_valid` returns a boolean instead of surfacing the query error.

`by_owner` matches either the live owner entity or the resolved thrower pawn.
`by_thrower` filters by player slot.

## Spawning

Generic creation:

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

`create` is an alias of `spawn`.

Typed helpers omit the type argument:

```text
spawn_he
spawn_flashbang
spawn_smoke
spawn_molotov
spawn_incendiary
spawn_decoy
spawn_inferno
```

Example:

```lua
local smoke = grenades.spawn_smoke(
    position,
    angles,
    velocity,
    { thrower = player, fuse = 1.5 }
)
```

Spawn options:

```text
owner         entity/index/player pawn used as owner
thrower       player object or slot
thrower_slot  explicit slot 0..63
fuse          -1, or a finite non-negative number of seconds
spawn         actual Lua boolean; defaults true
```

If `thrower` and `thrower_slot` are both supplied they must identify the same
slot.

All position/angle/velocity components must be finite. The native layer also
validates owner entities, thrower controllers/pawns, grenade type, fuse rules,
and the required Source 2 world services before creating anything.

## Projectile initialization

For projectile types, LuaCS initializes the current CS2 schema state before or
around spawn as required:

```text
m_vInitialPosition
m_vInitialVelocity
m_vecOriginalSpawnLocation
m_bIsLive
m_bDetonationRecorded
m_nBounces
m_hThrower
m_hOriginalThrower
m_iTeamNum
```

Incendiary and Molotov use the real shared `molotov_projectile` class.
Incendiary is identified by `CMolotovProjectile::m_bIsIncGrenade` rather than a
fabricated separate projectile class.

Fuse scheduling reads the engine-populated spawn time and writes the absolute
schema detonation time. Invalid/non-finite fuse values are rejected.

Inferno is already an active fire effect, not a projectile. It therefore
rejects projectile fuse scheduling.

## Grenade objects

Returned grenade objects expose:

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

`projectile` is false only for inferno. `effect` is true only for inferno.

Methods:

```lua
grenade:refresh()
grenade:is_valid()
grenade:detonate()
grenade:remove()
grenade:fuse_duration()
```

`refresh()` now updates the same Lua object in place and returns it. It no longer
silently replaces the caller's object with a different table.

`remove()` marks that Lua object `valid = false` after the native removal
succeeds.

`fuse_duration()` returns `detonate_time - spawn_time` when those fields form a
valid interval, otherwise `nil`.

## Detonation and removal

```lua
grenade:detonate()
grenades.detonate(grenade)

grenade:remove()
grenades.remove(grenade)
```

Projectile detonation uses the current schema detonation-time path rather than
assuming every grenade supports a Source 1-style `Detonate` input.

For inferno, detonation means ending the fire effect: LuaCS first tries the
`Extinguish` input and falls back to entity removal if necessary.

Calling detonate on an already exploded grenade returns an explicit error.

## Live state enrichment

The native adapter supplies the authoritative common grenade fields. The Lua
module additionally checks compatible live schema names for fields that vary
between projectile/effect classes, including thrower, team, bounce count, fire
count, lifetime, smoke-effect tick, and bounce-sound state.

Numeric conversions used for this enrichment are range checked; values outside
the LuaCS integer range are not silently truncated into an unrelated result.

## Native validation boundary

The final compiled advanced-world layer validates every grenade returned from
`get`, enumeration, and spawn. It rejects results with:

- invalid identity or type;
- an entity index that no longer resolves;
- non-finite position or velocity;
- non-finite timing fields;
- thrower slots outside `-1..63`;
- ownership entity indexes below `-1`;
- an empty classname.

This guard sits after the schema-native grenade implementation. It does not
replace projectile behavior or fabricate missing engine state.
