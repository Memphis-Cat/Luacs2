# LuaCS

**BETA TESTING 1.0**

LuaCS is a Windows x64 Metamod:Source runtime for writing Counter-Strike 2 dedicated-server plugins in Lua 5.5.1.

The beta currently includes:

- isolated Lua states per plugin;
- authenticated encrypted `.smg` plugin packages;
- plugin metadata and server-console load/unload/refresh commands;
- pre/post CS2 game events and LuaCS lifecycle events;
- timers and chat/command callbacks;
- live player/controller/pawn access;
- weapons, inventory, magazines, reserve ammo, and player ammo;
- HUD output and cvars;
- teams and round control;
- entity discovery/creation/input/transform operations;
- Source 2 sound emission;
- schema reflection and typed/raw properties;
- line, sphere, hull, capsule, and mesh traces;
- grenade projectile and inferno APIs.

This is the first beta-testing release. CS2 updates can change engine signatures, schema fields, and runtime behavior, so compatibility fixes should be expected during the beta period.

LuaCS keeps MSVC `/WX` enabled. Compiler warnings fail the native build instead of being hidden.

## Documentation

The API catalog is under [`docs/`](docs/README.md). Each public Lua module has its own folder with a README and `.lua` examples.

Main entry points:

- [`docs/plugins`](docs/plugins/) - plugin metadata, lifecycle, isolation, logs, and console management.
- [`docs/compiler`](docs/compiler/) - compiling `.lua` into `.smg`.
- [`docs/players`](docs/players/) - player state/actions.
- [`docs/weapons`](docs/weapons/) - inventory, clips, reserve ammo, player ammo, and slot replacement.
- [`docs/events`](docs/events/) - lifecycle and CS2 events, including `player_death`.
- [`docs/entities`](docs/entities/) - Source 2 entities.
- [`docs/properties`](docs/properties/) - schema properties/reflection.
- [`docs/traces`](docs/traces/) - collision traces.
- [`docs/grenades`](docs/grenades/) - grenade state and spawning.

See [`docs/README.md`](docs/README.md) for every module.

## Package layout

```text
addons/
|-- metamod/
|   `-- luacs2.vdf
`-- LuaCS/
    |-- bin/win64/
    |   |-- luacs2.dll
    |   |-- lua55.dll
    |   |-- events.dll
    |   |-- timers.dll
    |   |-- players.dll
    |   |-- commands.dll
    |   |-- math.dll
    |   |-- weapons.dll
    |   |-- hud.dll
    |   |-- cvars.dll
    |   |-- teams.dll
    |   |-- rounds.dll
    |   |-- entities.dll
    |   |-- sounds.dll
    |   |-- properties.dll
    |   |-- traces.dll
    |   `-- grenades.dll
    |-- scripting/
    |   |-- compile.exe
    |   `-- example_welcome.lua
    |-- plugins/
    |-- gamedata/reference/
    |-- config/
    `-- logs/
```

## Build

Run from an x64 Visual Studio Developer Command Prompt:

```bat
build.bat --no-deploy
```

The build fetches pinned public Metamod:Source, AlliedModders HL2SDK CS2, and SteamTracking protobuf revisions, generates the verified Source 2 binding sources, compiles with MSVC, and creates the package under `build/package`.

## Deploy

```bat
deploy.bat "F:\steamcmd2\steamapps\common\Counter-Strike Global Offensive\game\csgo"
```

Deployment preserves user plugin packages instead of mirroring/deleting the whole plugin directory.

## Compile a Lua plugin

Put a `.lua` file beside `addons/LuaCS/scripting/compile.exe` and run:

```bat
compile.exe myplugin.lua
```

The resulting package is written to `addons/LuaCS/plugins/myplugin.smg`.

On first use the compiler creates `addons/LuaCS/config/luacs.key`. Keep that key with the compiled packages. A different key cannot authenticate/decrypt packages created with the old one.

## Plugin metadata

```lua
plugin = {
    name = "My Plugin",
    author = "Byanca",
    version = "1.0.0",
    description = "Short description shown by lua plugins info."
}
```

Plugin version strings are chosen by each plugin author and are independent from the LuaCS runtime version.

## Server-console commands

```text
lua
lua help
lua help plugins
lua clear
lua version
lua plugins list
lua plugins info
lua plugins info <file.lua|file.smg|plugin name>
lua plugins load <file.lua|file.smg|plugin name>
lua plugins unload <file.lua|file.smg|plugin name>
lua plugins refresh <file.lua|file.smg|plugin name>
lua plugins retry
lua plugins force_load <file.lua|file.smg|plugin name>
lua plugins force_unload <file.lua|file.smg|plugin name>
```

`lua version` reports `LuaCS BETA TESTING 1.0` and the embedded Lua version separately.

## Modules

```lua
local events = require("cs2.events")
local timers = require("cs2.timers")
local players = require("cs2.players")
local commands = require("cs2.commands")
local math_api = require("cs2.math")
local weapons = require("cs2.weapons")
local hud = require("cs2.hud")
local cvars = require("cs2.cvars")
local teams = require("cs2.teams")
local rounds = require("cs2.rounds")
local entities = require("cs2.entities")
local sounds = require("cs2.sounds")
local properties = require("cs2.properties")
local traces = require("cs2.traces")
local grenades = require("cs2.grenades")
```

## Reporting beta problems

LuaCS uses explicit errors for unsupported or unresolved native operations. If the runtime fails to start or a plugin hits a native error, check `addons/LuaCS/logs/luacs-errors.log` and the plugin's own log file. Include the relevant error text and current CS2 server build when reporting the problem.