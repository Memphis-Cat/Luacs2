# `cs2.traces`

`cs2.traces` runs Source 2 collision queries using LuaCS's verified CS2 `Ray_t` adapter.

```lua
local traces = require("cs2.traces")
```

All vectors and floating-point inputs must be finite.

## Line/ray traces

```lua
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
ray -> line
segment -> line
from_direction -> direction
box -> hull
```

## Shapes

```text
traces.line(start_pos, end_pos [, options])
traces.sphere(start_pos, end_pos, center, radius [, options])
traces.hull(start_pos, end_pos, mins, maxs [, options])
traces.capsule(start_pos, end_pos, center_a, center_b, radius [, options])
traces.mesh(start_pos, end_pos, mins, maxs, vertices [, options])
```

Generic form:

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

Shape names:

```text
line / ray
sphere
hull / box
capsule
mesh
```

Constants:

```text
SHAPE_LINE
SHAPE_SPHERE
SHAPE_HULL
SHAPE_CAPSULE
SHAPE_MESH
```

`traces.shapes` maps the canonical lowercase names to the same IDs.

## Direction traces

```text
traces.direction(origin, direction, distance [, options])
```

Direction is normalized internally. Distance must be finite and non-negative. A positive distance requires a non-zero direction.

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

Boolean options require actual booleans.

`ignore`/`ignore_entities` accept entity indexes, entity-like tables with `entity_index`, player tables, or an array mixing those forms. Duplicate ignored entities are removed.

Limits:

```text
MAX_IGNORE_ENTITIES = 64
MAX_MESH_VERTICES = 256
```

Meshes require at least three vertices.

## Masks

LuaCS exposes individual `TRACE_*` interaction bits and common masks:

```text
MASK_SHOT_PHYSICS
MASK_SHOT_HITBOX
MASK_SHOT_FULL
MASK_WORLD_ONLY
MASK_GRENADE
MASK_PLAYER_MOVE
MASK_ALL
```

Unsigned 64-bit masks remain exact. High values are represented as decimal strings rather than floats. Input accepts non-negative integers or exact decimal/hex strings.

## Object sets

```text
OBJECTS_STATIC = 1
OBJECTS_KEYFRAMED = 2
OBJECTS_DYNAMIC = 4
OBJECTS_LOCATABLE = 8
OBJECTS_ALL_GAME_ENTITIES = 14
OBJECTS_ALL = 15
```

`traces.objects` maps the lowercase object-set names to these IDs.

## TraceResult

Important fields:

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

The boolean fields `hit_world` and `hit_entity` are data fields, so the verified module exposes callable helpers under distinct names:

```lua
result:did_hit()
result:did_hit_world()
result:did_hit_entity()
result:did_hit_entity(entity_or_player)
```

`did_hit_entity()` with no argument checks whether any entity was hit. With an argument it compares the result against the resolved entity/player index.

`fraction_left_solid_available` is currently false because Source 2 `CGameTrace` does not provide the old Source 1 `fractionleftsolid` value. LuaCS does not fabricate it.

LuaCS validates fractions, entity indexes, distances, vectors, masks, and returned native state before exposing a result.