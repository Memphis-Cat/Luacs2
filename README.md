# LuaCS

LuaCS is a Windows x64 Metamod:Source runtime that embeds Lua 5.5.1 for Counter-Strike 2 server plugins.

This branch is a real native foundation, not yet a complete CounterStrikeSharp-sized API. It implements the loader, authenticated compiler/container, isolated Lua states, logging, incremental compilation, lifecycle events, timers, client commands, connected-player metadata, and engine-backed weapons, HUD, and cvar modules. It deliberately does **not** expose health, armor, arbitrary entities, traces, grenades, rounds, teams, or sounds yet.

## Implemented layout

```text
addons/
├── metamod/
│   └── luacs2.vdf
└── LuaCS/
    ├── bin/win64/
    │   ├── luacs2.dll
    │   ├── lua55.dll
    │   ├── events.dll
    │   ├── timers.dll
    │   ├── players.dll
    │   ├── commands.dll
    │   ├── math.dll
    │   ├── weapons.dll
    │   ├── hud.dll
    │   └── cvars.dll
    ├── scripting/
    │   ├── compile.exe
    │   └── example_welcome.lua
    ├── plugins/
    ├── gamedata/reference/
    ├── config/
    └── logs/
```

## Build and deploy

Run `build.bat` from an x64 Visual Studio Developer Command Prompt. It fetches pinned public Metamod, AlliedModders HL2SDK CS2, and SteamTracking protobuf revisions; builds Release with Ninja; packages LuaCS; and deploys by default to:

```text
F:\steamcmd2\steamapps\common\Counter-Strike Global Offensive\game\csgo
```

Use `build.bat --no-deploy` to build/package without copying to a server. Override the deployment path by passing a game root:

```bat
build.bat "D:\servers\cs2\game\csgo"
```

The deploy step only writes `addons\LuaCS` and `addons\metamod\luacs2.vdf`. It does not replace Metamod's own DLLs or configuration.

## Compile Lua plugins

Put single-file `.lua` plugins beside `compile.exe` and run it. You can also drag one or more `.lua` files onto it.

The compiler parses with Lua 5.5.1, emits stripped bytecode, authenticates and encrypts `.smg` packages with AES-256-GCM, records the source SHA-256, skips unchanged sources, preserves the previous output after a syntax failure, and warns about exact deprecated names from the supplied API snapshot.

On first use it creates `config\luacs.key`. The same key is required to load the resulting `.smg` files. This obstructs casual extraction, but code cannot be made unrecoverable from a machine that possesses both the runtime and key.

## Lua API

Core globals are always available:

```lua
print("hello")
local cs2 = require("cs2")
local position = Vector(1, 2, 3)
local tint = Color(255, 80, 20, 255)
```

`require` uses Lua's normal cache, and the root module lazily exposes installed submodules:

```lua
local cs2 = require("cs2")
local same_players = cs2.players == require("cs2.players") -- true
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

Currently emitted events: `player_connect`, `player_activate`, `player_put_in_server`, `player_disconnect`, `client_command`, `map_start`, `map_end`, and `game_frame`.

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

The player module currently exposes connection metadata supplied by public server hooks:

```lua
local players = require("cs2.players")
local player = players.get_by_slot(0)

if player then
    print(player.slot, player.name, player.steam64)
end
```

Available fields: `slot`, `name`, `steamid`, `steam3`, `steam64`, `fake`, `connected`, and `active`. Health, armor, pawn objects, inventory enumeration, and arbitrary entity access are not exposed yet.

### Weapons

Weapon operations accept either a player table or an integer slot. Item names are exact CS2 classnames; LuaCS does not invent aliases.

```lua
local weapons = require("cs2.weapons")

local ok, error_message = weapons.give(player, "weapon_ak47")
if not ok then print(error_message) end

weapons.drop_active(player)
weapons.remove_all(player)
```

Implemented functions:

- `weapons.give(player_or_slot, classname)`
- `weapons.drop_active(player_or_slot)`
- `weapons.remove_all(player_or_slot)`

They resolve the live controller and pawn through Source 2's schema system and call the verified item-service virtual functions from the packaged Windows gamedata.

### HUD

```lua
local hud = require("cs2.hud")

hud.chat(player, "Hello in chat")
hud.center(player, "Center message")
hud.alert_all("Message for everyone")
hud.print(player, hud.CONSOLE, "Console message")
hud.broadcast(hud.NOTIFY, "Notification for everyone")
```

Destinations are `hud.NOTIFY`, `hud.CONSOLE`, `hud.CHAT`, `hud.CENTER`, and `hud.ALERT`. Every destination has a single-player and `_all` convenience function: `notify`, `console`, `chat`, `center`, `alert`, `notify_all`, `console_all`, `chat_all`, `center_all`, and `alert_all`.

HUD output uses CS2's verified `ClientPrint` and `UTIL_ClientPrintAll` functions from the packaged Windows gamedata.

### Cvars

```lua
local cvars = require("cs2.cvars")

if cvars.exists("mp_roundtime") then
    print(cvars.get_number("mp_roundtime"))
    local ok, error_message = cvars.set("mp_roundtime", 10)
    if not ok then print(error_message) end
end
```

Implemented functions: `exists`, `get`, `get_string`, `set`, `set_string`, `get_bool`, `get_int`, and `get_number`. Reads and writes use the real Source 2 `ICvar`/`ConVarRefAbstract` interface and return `nil, error` when the cvar is missing or conversion fails.

### Commands

Commands are matched from client console commands and `!command`/`/command` chat text:

```lua
local commands = require("cs2.commands")

commands.on("hello", function(player, arguments, raw)
    print(player and player.name or "unknown", arguments, raw)
end)
```

## Logging

Each compiled plugin gets a numeric log filename in `logs`. `print(...)` writes to that file and the server console. Example:

```text
5820261129501.log
```

## Reference data

`gamedata/reference` preserves the supplied Valve `cs_script` API snapshot and Windows gamedata pack. The 735-row API JSON is restored byte-for-byte from hash-verified chunks during `build.bat`. Declarations are never silently treated as working Metamod functions: engine-facing APIs are added only when backed by a real interface, Source 2 schema resolution, or verified gamedata.

The CI build validates native compilation and compiler behavior. Loading and exercising a build on a live CS2 server remains a separate runtime test.
