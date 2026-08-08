# `cs2.cvars`

`cs2.cvars` reads and writes Source 2 console variables through the live server cvar system.

```lua
local cvars = require("cs2.cvars")
```

## API

```text
cvars.exists(name)
cvars.get(name)
cvars.get_string(name)       -- alias of get
cvars.set(name, value)
cvars.set_string(name, value) -- alias of set
cvars.get_bool(name)
cvars.get_int(name)
cvars.get_number(name)
```

## Existence

```lua
if cvars.exists("sv_cheats") then
    print("sv_cheats exists")
end
```

## String reads and writes

`get` returns the engine's text representation.

```lua
local value, err = cvars.get("mp_roundtime")
```

`set` converts its second argument to text. Embedded NUL bytes are rejected.

```lua
local ok, err = cvars.set("mp_roundtime", 60)
```

## Typed reads

### `get_bool`

Recognized true values:

```text
1
true
yes
on
```

Recognized false values:

```text
0
false
no
off
```

Comparison is case-insensitive.

### `get_int`

Requires the whole cvar string to be an integer. Partial values such as `10abc` are rejected.

### `get_number`

Parses a finite floating-point number. Overflow, NaN, infinity, empty strings, or trailing characters are rejected.

Read failures return `nil, error_message`.