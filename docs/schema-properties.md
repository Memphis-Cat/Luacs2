# LuaCS schema properties

`cs2.properties` is LuaCS's reflection and typed read/write layer over the live
Source 2 `server.dll` schema. It operates on real entity memory discovered from
the entity's dynamic schema binding. Unsupported representations fail with an
explicit error instead of being guessed or silently reinterpreted.

The advanced world ABI remains v3. The public Lua module adds convenience on
top of the existing bounded property service; no schema pointer is exposed to
Lua.

## Targets

Every property function accepts an entity index or a table that can identify an
entity. Entity, weapon, grenade, and entity-handle tables use `entity_index`.
Player tables prefer a live `pawn_index` and fall back to `controller_index`.
For a controller-only property, pass `player.controller_index` explicitly.

```lua
local properties = require("cs2.properties")

local health = properties.get(player, "m_iHealth")
local ping = properties.get(player.controller_index, "m_iPing")
```

Invalid or negative entity indexes are rejected.

## Property paths

Paths can address inherited fields, embedded classes, pointers, fixed arrays,
and dynamic schema collections.

```lua
properties.get(entity, "m_iHealth")
properties.get(entity, "m_embedded.m_flValue")
properties.get(entity, "m_array[2]")
properties.get(entity, "m_array", 2)
properties.get(entity, "m_collection", 3)
properties.get(entity, "m_objects[1].m_nState")
```

Schema indexes are zero-based because they address C++ array/collection
elements. A path index and the separate index argument may not disagree. If the
final path segment already contains `[n]`, do not also pass the index argument.

An index on an earlier path segment applies only to that segment. For example,
`m_objects[2].m_iHealth` selects object 2 and does not accidentally apply index
2 to `m_iHealth`.

Typed access to a non-character fixed-array root or a dynamic-collection root
requires an element index. Fixed `char[]` fields remain normal string
properties and do not require an index.

## Core API

```text
info(entity, path [, index])
exists(entity, path [, index])
kind(entity, path [, index])
get(entity, path [, index])
set(entity, path, value [, index] [, network=true])
get_raw(entity, path [, index])
set_raw(entity, path, bytes [, index] [, network=true])
count(entity, path)
values(entity, path)
get_all(entity, path)                 -- alias of values
collection_count(entity, path)
collection_resize(entity, path, size [, network=true])
list(entity [, inherited=true])
children(entity [, path=""] [, inherited=true])
walk(entity [, path=""] [, inherited=true] [, max_depth=4])
ref(entity, path [, index])
```

`count` works for both fixed arrays and dynamic collections. `values` returns a
normal Lua 1-based sequence containing the zero-based schema elements in order.
`collection_resize` is only for dynamic collections; after requesting a resize,
LuaCS asks Source 2 for the count again and reports failure if the requested
size was not applied.

`walk` recursively reflects embedded/pointer classes to a maximum depth from 0
to 16. The default is 4. A null pointer remains visible as metadata, but deeper
reflection through that unavailable pointer branch is skipped instead of
aborting the entire walk.

## Typed API

Generic `get`/`set` infer the schema kind. The typed forms additionally require
that the property kind matches the requested operation.

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

Examples:

```lua
local hp, err = properties.get_int(player, "m_iHealth")
if hp then
    properties.set_int(player, "m_iHealth", hp + 25)
end

properties.set_vector(entity, "m_vecVelocity", Vector(0, 0, 300))
properties.set_handle(entity, "m_hOwnerEntity", other_entity)
properties.set_handle(entity, "m_hOwnerEntity", nil) -- clear handle
```

Boolean setters require an actual Lua boolean. Float, Vector, and QAngle writes
reject NaN and infinity. Entity-handle writes verify that the target entity
exists before writing the handle.

Signed and unsigned integer writes are checked against the schema field's real
1/2/4/8-byte width. Values that do not fit are rejected; LuaCS never truncates
them into the field.

## Exact unsigned and pointer values

Lua 5.5 integers are signed. An unsigned 64-bit schema value or pointer that is
at most `lua_Integer`'s maximum is returned as a Lua integer. A larger value is
returned as an exact decimal string rather than an imprecise floating-point
number.

Setters accept either a non-negative Lua integer or an exact decimal/hex string:

```lua
properties.set_uint(entity, "m_nBigValue", "18446744073709551615")
properties.set_pointer(entity, "m_pSomething", "0x7FF612341234")
```

A string beginning with `0x`/`0X` is hexadecimal. All other strings are parsed
as decimal, including strings with leading zeroes.

## Kinds

`info.kind` and the first result of `properties.kind` use these names:

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

Numeric constants remain available:

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

`properties.kinds` maps the string names to those numeric IDs.

## Metadata

`properties.info`, `list`, `children`, `walk`, and property references expose
metadata including:

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

`owner_class` identifies the class that declares the field when that information
is available. `networked` is derived from Source 2 schema metadata. `offset` is
the live entity-relative offset when that is meaningful; fields reached through
a pointer do not pretend that the pointed-to allocation has a normal offset
inside the root entity.

## Property references

A reference stores one entity/path/index and its current metadata:

```lua
local health = properties.ref(player, "m_iHealth")

print(health.kind, health.type_name, health.networked)
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

`count`, `values`, and `resize` reject a reference that already points to one
indexed element.

## Fixed arrays and collections

Both of these work:

```lua
local count = properties.count(entity, "m_values")
local first = properties.get(entity, "m_values", 0)
local second = properties.get(entity, "m_values[1]")
local all = properties.values(entity, "m_values")
```

For dynamic collections:

```lua
local old_count = properties.collection_count(entity, "m_entries")
properties.collection_resize(entity, "m_entries", old_count + 1)
properties.set(entity, "m_entries", new_value, old_count)
```

Collection manipulation is only exposed when Source 2 provides a schema
collection manipulator. Bounds are validated before element access.

## Raw access

`get_raw` returns two results: the exact byte string and the same metadata table
used by `info`.

```lua
local bytes, meta = properties.get_raw(entity, "m_unknownField")
print(#bytes, meta.byte_size, meta.type_name)
```

Raw values are bounded to 4096 bytes. `set_raw` requires the byte count to match
the exact stable schema size. Dynamic collection roots require an element index
for raw access.

Raw access is intentionally low-level. It is useful for schema representations
that LuaCS cannot safely map to a typed value, but it does not bypass size,
entity, path, collection, or network-state validation.

### Bitfields

Source 2's public schema type exposes a bitfield's bit count but not a stable
public field-level bit offset. LuaCS therefore exposes a stable bitfield storage
size as `raw` for inspection but marks it non-writable. Both typed writes and
`set_raw` refuse bitfield mutation so one property cannot accidentally overwrite
neighboring bits.

## Strings

Supported typed strings include fixed schema `char[]`, `CUtlString`, and
`CUtlSymbolLarge` forms recognized by the live schema type.

Fixed strings are never silently truncated. If the value plus its terminator
does not fit the fixed array, the write fails. LuaCS's cross-module typed string
buffer is bounded to 511 data bytes plus the terminator.

## Network-state notification

Mutating operations default to `network=true`. When enabled, LuaCS calls
`NetworkStateChanged` on the root entity using the root schema field offset and,
when the networked root itself is indexed, the corresponding root array index.
Nested indexes are not incorrectly reused for unrelated child fields.

Pass `false` only when intentionally performing a server-only/internal write:

```lua
properties.set(entity, "m_iHealth", 100, nil, false)
properties.set_raw(entity, "m_field", bytes, nil, false)
```

The optional network/inherited flags are strict Lua booleans. Values such as
`0`, `1`, or strings are rejected instead of relying on Lua truthiness.

## Error model

Mutating operations return `true` on success. Reads return their value. Normal
engine/schema failures return `nil, error_message`.

Lua argument/type errors use normal Lua argument errors. Examples of explicitly
rejected operations include:

- missing/invalid entity indexes;
- unknown property paths;
- null pointer descent;
- array or collection indexes out of range;
- typed access to an aggregate root without an index;
- kind mismatches;
- signed/unsigned overflow;
- non-finite float/vector/angle values;
- invalid entity-handle targets;
- fixed-string truncation;
- raw size mismatches or values over 4096 bytes;
- unsafe bitfield writes;
- collection resize requests Source 2 does not actually apply.

No unsupported schema representation is silently simulated.
