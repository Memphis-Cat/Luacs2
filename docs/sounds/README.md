# `cs2.sounds`

`cs2.sounds` emits and stops Source 2 sounds for selected players.

```lua
local sounds = require("cs2.sounds")
```

## Emit to selected recipients

```text
sounds.emit(recipients, sound_name [, options])
sounds.emit_to(recipients, sound_name [, options])
```

`emit_to` is an alias-style convenience form of the same recipient-based operation.

Recipients may be:

- a player table;
- a slot number;
- a Lua sequence containing player tables and/or slots.

```lua
local sound, err = sounds.emit(player, "sounds/example.vsnd_c")
```

## Emit to everyone

```text
sounds.emit_all(sound_name [, options])
```

The recipient set is built from currently connected players.

## Options

```text
source
origin
volume
pitch
delay
channel
reliable
```

`source` can be an entity index, entity table, or player table. A player source resolves to its live pawn.

`origin` is a finite vector.

`volume` must be finite and within `0..10`.

`pitch` must be `1..255`.

`delay` must be finite and within `0..3600` seconds.

`channel` must fit a signed 32-bit integer.

`reliable` must be a Lua boolean.

## Sound objects

A successful emit returns:

```text
valid
guid
stack_hash
source_entity_index
recipients_mask
channel
name
```

The recipient mask is an exact unsigned 64-bit value. Large masks are represented as decimal strings rather than losing precision in a Lua float.

A sound object has:

```lua
sound:stop()
```

## Stop

```text
sounds.stop(sound_or_guid [, recipients [, reliable=true]])
sounds.stop_channel(channel [, recipients [, reliable=true]])
```

If a sound object is passed to `stop`, its original recipient mask is used by default. After a successful stop, the object's `valid` field is set to `false`.

`stop_channel` returns the number of stopped sounds.

An empty recipient filter is rejected instead of silently broadcasting.