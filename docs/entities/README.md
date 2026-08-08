# `cs2.entities`

`cs2.entities` works with live Source 2 server entities.

```lua
local entities = require("cs2.entities")
```

An entity can be passed as an entity table or a non-negative entity index.

## Entity fields

A refreshed entity contains:

```text
valid
spawned
entity_index
handle
health
team
owner_index
parent_index
classname
name
position
angles
velocity
owner
parent
```

`owner` and `parent` are resolved entity tables when those relationships point to a valid entity; otherwise they are `nil`.

## Lookup and discovery

```text
entities.get(entity_index)
entities.refresh(entity)
entities.is_valid(entity)
entities.all()
entities.find_by_classname(pattern)
entities.find_by_name(pattern)
entities.find_all_by_name(pattern)
entities.count_by_classname(pattern)
entities.count_by_name(pattern)
```

The Source 2 entity discovery service handles the pattern syntax. `find_by_name` returns the first match; `find_all_by_name` returns all matches.

```lua
local weapons = entities.find_by_classname("weapon_*")
for _, entity in ipairs(weapons) do
    print(entity.entity_index, entity.classname)
end
```

## Create

```text
entities.create(classname [, options])
```

Supported options:

```text
position
angles
velocity
spawn
```

Example:

```lua
local entity, err = entities.create("prop_dynamic", {
    position = Vector(0, 0, 128),
    angles = Vector(0, 90, 0),
    spawn = true,
})
```

Creation is transactional. If transform setup, spawn, or final state validation fails, LuaCS attempts to remove the entity it just created instead of leaving a half-initialized object.

## Spawn and remove

```text
entities.spawn(entity)
entities.remove(entity)
```

Object form:

```lua
entity:spawn()
entity:remove()
```

After a successful remove, an entity table passed by the caller is marked `valid = false`.

## Position, angles, velocity

Generic form:

```text
entities.teleport(entity, position, angles, velocity)
```

Any transform argument may be `nil`, but at least one must be provided.

Object helpers:

```lua
entity:set_position(Vector(100, 0, 64))
entity:set_angles(Vector(0, 180, 0))
entity:set_velocity(Vector(0, 0, 300))
```

These helper methods exist on entity objects. The module-level API exposes the generic `teleport` function.

All vector components must be finite.

## Ownership and parenting

```text
entities.set_owner(entity, owner_or_nil)
entities.set_parent(entity, parent_or_nil)
```

Object form:

```lua
entity:set_owner(other)
entity:set_parent(parent)
entity:clear_parent()
```

Passing `nil` clears the corresponding relationship.

## Entity inputs

```text
entities.input(entity, input_name [, value="" [, activator=nil [, caller=nil [, delay=0]]]])
```

Example:

```lua
entities.input(door, "Open", "", player.pawn_index, player.pawn_index, 0)
```

Input names cannot be empty or contain NUL bytes. Delay must be finite and non-negative.

## Stale entities

Entity indexes can become invalid at any time when Source 2 deletes an entity. Use `entity:is_valid()` or `entities.is_valid(entity)` before relying on an object captured earlier, and call `refresh()` when you need current fields.

Normal engine failures return `nil, error_message`.