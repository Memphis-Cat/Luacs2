# LuaCS traces

`cs2.traces` exposes Source 2 collision queries through the pinned CS2 HL2SDK
`Ray_t`, `CTraceFilter`, and `CGameTrace` layouts used by LuaCS. The native
adapter validates the request before calling Source 2 and the final runtime
boundary rejects malformed or non-finite engine results before Lua receives
them.

The AdvancedWorld ABI remains v3.

## Basic traces

```lua
local traces = require("cs2.traces")

local result, err = traces.line(
    Vector(0, 0, 64),
    Vector(1000, 0, 64),
    {
        mask = traces.MASK_SHOT_FULL,
        ignore = player,
    }
)
```

Aliases:

```text
ray       -> line
segment   -> line
box       -> hull
from_direction -> direction
```

All vector components and floating-point inputs must be finite.

## Shapes

All five supported Source 2 query shapes are exposed:

```lua
traces.line(start_pos, end_pos [, options])
traces.sphere(start_pos, end_pos, center, radius [, options])
traces.hull(start_pos, end_pos, mins, maxs [, options])
traces.capsule(start_pos, end_pos, center_a, center_b, radius [, options])
traces.mesh(start_pos, end_pos, mins, maxs, vertices [, options])
```

Generic requests use `cast`:

```lua
local result = traces.cast({
    shape = "capsule",
    start = Vector(0, 0, 64),
    ["end"] = Vector(1000, 0, 64),
    center_a = Vector(0, 0, -18),
    center_b = Vector(0, 0, 18),
    radius = 16,
})
```

`shape` accepts either a numeric `SHAPE_*` constant or one of:

```text
line / ray
sphere
hull / box
capsule
mesh
```

The numeric constants remain:

```text
SHAPE_LINE
SHAPE_SPHERE
SHAPE_HULL
SHAPE_CAPSULE
SHAPE_MESH
```

`traces.shapes` maps the canonical string names to the same IDs.

## Direction traces

`direction` normalizes a direction vector and traces an exact distance:

```lua
local result = traces.direction(
    origin,
    Vector(1, 0.25, 0),
    2048,
    { ignore = player }
)
```

A positive distance requires a non-zero direction. Distance must be finite and
non-negative.

## Options

```text
mask / contents / interacts_with
interacts_as
interacts_exclude
collision_group
object_set_mask
hit_solid
hit_solid_requires_generate_contacts
hit_triggers
ignore_disabled_pairs
ignore_if_both_hitboxes
force_hit_everything
iterate_entities
hit_entities
included_detail_layers
target_detail_layer
ignore
ignore_entities
center / center_a / center_b
radius
mins / maxs
vertices / mesh_vertices
shape
```

Boolean options require actual Lua booleans. Numeric shape detail fields are
range checked before entering the engine.

`ignore` and `ignore_entities` accept:

- an entity index;
- entity/weapon/grenade/handle tables with `entity_index`;
- player tables with `pawn_index` or `controller_index`;
- a Lua sequence containing any mixture of those forms.

Duplicate ignored entities are removed. The maximum is:

```text
MAX_IGNORE_ENTITIES = 64
```

Meshes support at most:

```text
MAX_MESH_VERTICES = 256
```

The native adapter also rejects meshes with fewer than three vertices,
degenerate bounds, invalid capsules/hulls, non-positive radii, invalid entity
indexes, unsupported object-set bits, and collision groups outside the Source 2
byte range.

## Exact 64-bit masks and pointers

Lua integers are signed, while Source 2 interaction masks are unsigned 64-bit.
LuaCS never converts a high unsigned value to an imprecise Lua float.

Values through `INT64_MAX` are Lua integers. Larger values are exact decimal
strings. Therefore `MASK_ALL` is represented exactly rather than wrapping to
`-1`.

Trace options accept either a non-negative Lua integer or an exact decimal/hex
string:

```lua
local result = traces.line(start_pos, end_pos, {
    mask = "18446744073709551615",
    interacts_with = "0x8000000000",
})
```

The same exact-value rule applies to these result fields:

```text
contents64
physics_body
physics_shape
shape_interacts_as
shape_interacts_with
shape_interacts_exclude
```

## Object-set constants

Pinned CS2 `RnQueryObjectSet` values:

```text
OBJECTS_STATIC = 1
OBJECTS_KEYFRAMED = 2
OBJECTS_DYNAMIC = 4
OBJECTS_LOCATABLE = 8
OBJECTS_ALL_GAME_ENTITIES = 14
OBJECTS_ALL = 15
```

`traces.objects` exposes the same values by canonical lowercase names.

## Common masks

LuaCS exposes the pinned interaction bits individually as `TRACE_*` constants
and includes these common compositions:

```text
MASK_SHOT_PHYSICS
MASK_SHOT_HITBOX
MASK_SHOT_FULL
MASK_WORLD_ONLY
MASK_GRENADE
MASK_PLAYER_MOVE
MASK_ALL
```

No mask is silently narrowed to signed 32-bit or floating point.

## Trace results

Every successful trace returns a `TraceResult` object. Important fields:

```text
valid
hit
start_solid
all_solid
exact_hit_point
fraction
fraction_left_solid
fraction_left_solid_available
start
end / end_position
position / hit_position
normal / plane_normal
plane_distance
distance
total_distance
remaining_distance
hit_offset
entity_index
entity_handle
hit_entity
hit_world
hitbox
hitgroup
surface_name
surface_flags
contents
contents64
triangle
bone
shape
shape_name
physics_body
physics_shape
shape_interacts_as
shape_interacts_with
shape_interacts_exclude
shape_entity_id
shape_owner_id
shape_hierarchy_id
shape_detail_layer_mask
shape_detail_layer_mask_type
shape_target_detail_layer
shape_collision_group
shape_collision_function_mask
```

`hit_entity` and `hit_world` are boolean fields. The callable helpers use
distinct names so those fields cannot shadow methods:

```lua
result:did_hit()
result:did_hit_world()
result:did_hit_entity()
result:did_hit_entity(entity_or_player)
```

`did_hit_entity()` with no argument answers whether any entity was hit. With an
argument it compares against that entity's resolved index.

`fraction_left_solid_available` remains `false`: Source 2 `CGameTrace` does not
provide Source 1's `fractionleftsolid`. LuaCS does not fabricate it.

`total_distance` is the requested segment length. `distance` is the distance to
the returned hit position. `remaining_distance` is the non-negative difference.

## Native validation boundary

The final compiled advanced-world layer keeps the existing verified `Ray_t`
trace implementation and then validates its output. It rejects:

- an invalid result flag;
- shape IDs outside the ABI range;
- fractions outside `0..1`;
- negative/non-finite distance;
- non-finite hit offsets or plane distances;
- non-finite start/end/hit/normal vectors;
- miss results that unexpectedly retain a hit entity;
- entity indexes below `-1`.

This validation is defensive. It does not replace the Source 2 trace itself and
it does not simulate collisions.
