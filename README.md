# LuaCS

LuaCS is a Windows x64 Metamod:Source runtime that embeds Lua 5.5.1 for Counter-Strike 2 server plugins.

This branch is the first real foundation, not the complete CounterStrikeSharp-sized API. It implements the loader, compiler/container, native-module ABI, per-plugin Lua states, logging, incremental compilation, timers, connection events, client-command callbacks, and connected-player metadata. It deliberately does **not** expose health, weapons, entities, traces, grenades, rounds, sounds, or undocumented memory operations yet.

## Implemented layout

```text
addons/
├── metamod/
│   └── luacs2.vdf
└── LuaCS/
    ├── bin/
    │   ├── luacs2.dll
    │   ├── lua55.dll
    │   ├── events.dll
    │   ├── timers.dll
    │   ├── players.dll
    │   ├── commands.dll
    │   └── math.dll
    ├── scripting/
    │   ├── compile.exe  (Lua is linked statically, so no DLL is required beside it)
    │   └── example_welcome.lua
    ├── plugins/
    ├── gamedata/
    ├── config/
    └── logs/
```

## Build and deploy

Run `build.bat` from an x64 Visual Studio Developer Command Prompt. It fetches the public Metamod source tree and the `cs2` branch of AlliedModders HL2SDK, builds Release with Ninja, packages LuaCS, and deploys to:

```text
F:\steamcmd2\steamapps\common\Counter-Strike Global Offensive\game\csgo
```

Use `build.bat --no-deploy` to build/package without copying to a server. Override the deployment path by passing a game root:

```bat
build.bat "D:\servers\cs2\game\csgo"
```

The deploy step only writes `addons\LuaCS` and `addons\metamod\luacs2.vdf`. It does not replace Metamod's DLLs, `metaplugins.ini`, README, or other plugin descriptors.

## Compile Lua plugins

Put single-file `.lua` plugins beside `compile.exe` and run it. You can also drag one or more `.lua` files onto it.

The compiler:

- parses the source with Lua 5.5.1;
- reports Lua's exact syntax error and line;
- emits stripped Lua bytecode;
- encrypts and authenticates it as `.smg` with AES-256-GCM;
- stores the source SHA-256 in the header;
- skips an unchanged source when the existing `.smg` hash matches;
- never replaces the previous `.smg` when compilation fails;
- warns when exact deprecated names from the supplied API snapshot appear in source.

On first compiler use—or on a clean runtime installation with no existing `.smg` files—it creates `config\luacs.key`. The same key is required to load the resulting `.smg` files. The runtime refuses to invent a replacement key when compiled plugins already exist. This makes casual decompilation harder, but it is not mathematically impossible to recover code from a machine that has both the runtime and key.

## Lua API

Core globals are always available:

```lua
print("hello")
local cs2 = require("cs2")
local events = require("cs2.events")
local timers = require("cs2.timers")
local position = Vector(1, 2, 3)
local tint = Color(255, 80, 20, 255)
```

`require` is cached by Lua, so two imports return the same table:

```lua
local a = require("cs2.players")
local b = require("cs2.players")
print(a == b) -- true
```

The root module lazily exposes installed submodules:

```lua
local cs2 = require("cs2")
local player = cs2.players.get_by_slot(0)
```

### Events

```lua
local events = require("cs2.events")

events.on("player_connect", function(event)
    print(event.player.name, event.player.steam64)
end)

events.Instance:OnPlayerActivate(function(player)
    print(player.name, "is active")
end)
```

Currently emitted events:

- `player_connect`
- `player_activate`
- `player_put_in_server`
- `player_disconnect`
- `client_command`
- `map_start`
- `map_end`
- `game_frame`

### Timers

```lua
local timers = require("cs2.timers")

timers.after(1.0, function()
    print("one second later")
end)

local id = timers.every(5.0, function()
    print("every five seconds")
end)

timers.cancel(id)
```

### Players

The current player module exposes only information supplied by public connection hooks:

```lua
local players = require("cs2.players")
local player = players.get_by_slot(0)

if player then
    print(player.name)
    print(player.steamid)
    print(player.steam3)
    print(player.steam64)
end
```

Available fields: `slot`, `name`, `steamid`, `steam3`, `steam64`, `fake`, `connected`, and `active`.

`customid`, profile state/creation time, health, armor, pawn, weapons, and entity operations are not exposed in this foundation because the public hook data does not provide them.

### Commands

Commands are matched from client console commands and `!command`/`/command` chat text:

```lua
local commands = require("cs2.commands")

commands.on("hello", function(player, arguments, raw)
    print(player and player.name or "unknown", arguments, raw)
end)
```

## Logging

Each compiled plugin gets a log file in `logs`. `print(...)` writes to that file and the server console. The filename is numeric: unpadded day, month, year, hour, minute, second, then a collision index, for example:

```text
5820261129501.log
```

## Correct Lua syntax

Use:

```lua
events.Instance:OnPlayerConnect(function(player)
    print(player.name)
end)
```

This is not valid Lua:

```lua
event.Instance.OnPlayerConnect:function(player)
```

## Reference data

`gamedata/reference` preserves the uploaded Valve `cs_script` API snapshot and Windows gamedata pack as design/reference inputs. The 735-row API JSON is restored byte-for-byte from four hash-verified archive chunks at the beginning of `build.bat`. Those declarations are not silently treated as Metamod functions. Engine-facing modules are added only when a real public interface or verified gamedata-backed implementation exists.
