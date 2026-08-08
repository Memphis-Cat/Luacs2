# `cs2.properties`

`cs2.properties` is LuaCS's Source 2 schema reflection and property read/write module.

Use it when a field is not already exposed by a higher-level module, or when you need schema metadata.

```lua
local properties = require("cs2.properties")
```

## Targets

Property functions accept an entity index or a table that identifies an entity.

Entity, weapon, grenade, and entity-handle tables use `entity_index`. Player tables prefer `pawn_index` and fall back to `controller_index`. For a controller-only field, pass `player.controller_index` explicitly.

## Paths

Paths support inherited fields, embedded classes, pointers, fixed arrays, and dynamic collections.

```lua
properties.get(entity, "m_iHealth")
properties.get(entity, "m_array[2]")
properties.get(entity, "m_array", 2)
properties.get(entity, "m_objects[1].m_nState")
```

Schema indexes are zero-based. A path index and the separate index argument may not disagree.

Typed access to a non-character fixed-array root or collection root requires an element index. Fixed `char[]` fields remain string properties.

## Core API

```text
properties.info(entity, path [, index])
properties.exists(entity, path [, index])
properties.kind(entity, path [, index])
properties.get(entity, path [, index])
properties.set(entity, path, value [, index] [, network=true])
properties.get_raw(entity, path [, index])
properties.set_raw(entity, path, bytes [, index] [, network=true])
properties.count(entity, path)
properties.values(entity, path)
properties.get_all(entity, path)
properties.collection_count(entity, path)
properties.collection_resize(entity, path, size [, network=true])
properties.list(entity [, inherited=true])
properties.children(entity [, path=""] [, inherited=true])
properties.walk(entity [, path=""] [, inherited=true] [, max_depth=4])
properties.ref(entity, path [, index])
```

`get_all` is an alias of `values`.

`walk` accepts depth `0..16`. Null pointer branches remain visible as metadata, but LuaCS does not dereference through a null pointer.

## Typed getters

```text
get_boolean / get_bool
get_integer / get_int
get_unsigned / get_uint
get_float
get_string
get_vector
get_angle
get_handle
get_pointer
```

## Typed setters

```text
set_boolean / set_bool
set_integer / set_int
set_unsigned / set_uint
set_float
set_string
set_vector
set_angle
set_handle
set_pointer
```

Example:

```lua
local health, err = properties.get_int(player, "m_iHealth")
if health then
    properties.set_int(player, "m_iHealth", health + 10)
end
```

Boolean setters require booleans. Float/vector/angle writes reject NaN and infinity. Integer writes are checked against the schema field's actual width instead of truncating.

Handle writes validate the target entity. Passing `nil` clears a supported entity handle.

## Kinds

Names:

```text
boolean
integer
unsigned_integer
float
string
vector
angle
entity_handle
pointer
raw
```

Numeric constants:

```text
BOOLEAN
SIGNED_INTEGER
UNSIGNED_INTEGER
FLOAT
STRING
VECTOR
ANGLE
ENTITY_HANDLE
POINTER
RAW
```

`properties.kinds` maps canonical names to IDs.

## Metadata

Reflection returns fields such as:

```text
valid
name
path
type_name
owner_class
kind
kind_name
kind_id
offset
byte_size
element_size
array_count
selected_index
networked
readable
writable
fixed_array
collection
pointer
embedded_class
indexable
aggregate
typed_readable
typed_writable
raw_readable
raw_writable
```

## Exact unsigned values and pointers

Lua integers are signed. Unsigned 64-bit values and pointers up to `INT64_MAX` are Lua integers. Larger values are returned as exact decimal strings.

Setters accept a non-negative integer or exact decimal/hex string:

```lua
properties.set_uint(entity, "m_nBigValue", "18446744073709551615")
properties.set_pointer(entity, "m_pSomething", "0x7FF612341234")
```

## Property references

```lua
local health = properties.ref(player, "m_iHealth")
print(health:get())
health:set(150)
health:refresh()
```

Methods:

```text
ref:refresh()
ref:get()
ref:set(value [, network=true])
ref:get_raw()
ref:set_raw(bytes [, network=true])
ref:count()
ref:values()
ref:resize(size [, network=true])
ref:children([inherited=true])
```

## Arrays and collections

```lua
local total = properties.count(entity, "m_values")
local first = properties.get(entity, "m_values", 0)
local all = properties.values(entity, "m_values")
```

Dynamic collection resizing is only available when Source 2 provides a collection manipulator. LuaCS verifies the count after resize and reports failure if the engine did not apply the requested size.

## Raw access

`get_raw` returns the exact byte string and its metadata. Raw values are limited to 4096 bytes. `set_raw` requires the byte count to match the stable schema size.

Bitfield storage can be inspected as raw data but is not writable because the public schema does not expose a stable field-level bit offset.

## Network notification

Writes default to `network=true`. LuaCS notifies Source 2 that the root networked field changed using the correct root offset/index. Pass `false` only when you deliberately want a server-only/internal write.

## Failure behavior

Unknown paths, out-of-range indexes, null pointer descent, kind mismatches, numeric overflow, unsafe bitfield writes, fixed-string truncation, invalid handles, oversized raw data, and unsupported collection operations return explicit errors. LuaCS does not guess unsupported schema layouts.