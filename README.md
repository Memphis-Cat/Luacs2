# LuaCS

LuaCS is a Windows x64 Metamod:Source runtime that embeds Lua 5.5.1 for Counter-Strike 2 dedicated-server plugins.

Version `0.5.0` includes:

- isolated Lua states and authenticated encrypted `.smg` packages;
- server-console plugin administration through the `lua` command;
- plugin name, author, version, and description metadata;
- timers, commands, lifecycle events, and mutable CS2 pre/post game events;
- live player/controller/pawn state and actions;
- weapons, inventory, clips, reserve ammo, and player ammo access;
- HUD output and Source 2 cvars;
- team, round, entity, sound, schema-property, trace, and grenade APIs.

Unsupported or unresolved operations return explicit errors. The Windows build keeps MSVC `/WX` enabled, so compiler warnings fail the build instead of being hidden.

## Package layout

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
    │   ├── cvars.dll
    │   ├── teams.dll
    │   ├── rounds.dll
    │   ├── entities.dll
    │   ├── sounds.dll
    │   ├── properties.dll
    │   ├── traces.dll
    │   └── grenades.dll
    ├── scripting/
    │   ├── compile.exe
    │   └── example_welcome.lua
    ├── plugins/
    ├── gamedata/reference/
    ├── config/
    └── logs/
```

## Build and deploy

Run from an x64 Visual Studio Developer Command Prompt:

```bat
build.bat --no-deploy
powershell -NoProfile -ExecutionPolicy Bypass -File ".\tests\compiler-smoke.ps1"
```

Deploy to a CS2 `game\csgo` directory after both commands succeed:

```bat
deploy.bat "F:\steamcmd2\steamapps\common\Counter-Strike Global Offensive\game\csgo"
```

The build fetches pinned public Metamod, AlliedModders HL2SDK CS2, and SteamTracking protobuf revisions. Deterministic source fixes are validated before compilation rather than suppressing diagnostics.

## Compile Lua plugins

Put single-file `.lua` plugins beside `compile.exe` and run it, or drag Lua files onto it. The compiler emits authenticated encrypted `.smg` packages into `plugins`.

On first use it creates `config\luacs.key`. Keep the same key with the compiled packages. A plugin is deployed as `example.smg`, but all administration commands also accept its original `example.lua` name or filename stem.

## Plugin metadata

Declare metadata through the global `plugin` table:

```lua
plugin = {
    name = "My Plugin",
    author = "Byanca",
    version = "1.0.0",
    description = "A short description shown by lua plugins info."
}
```

All fields are optional. Missing fields fall back to the package filename, `Unknown`, or `Unspecified`.

A normal unload first calls one supported cleanup callback:

```lua
function OnUnload()
    print("Cleaning up")
end
```

or:

```lua
function plugin:unload()
    print("Cleaning up " .. self.name)
end
```

If that callback errors, normal unload/refresh is refused and the real Lua error is logged. `force_unload` and `force_load` intentionally skip the cleanup callback.

## Server-console commands

These commands are registered in the CS2 dedicated-server console:

```text
lua
lua help
lua help plugins
lua help plugins force_load
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

`retry` retries only packages whose most recent load failed. `list` and `info` show loaded, unloaded, failed, or missing state; failed entries include their last load error. Declared plugin names become available after the package has loaded successfully at least once.

## Modules

```lua
local cs2 = require("cs2")
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

The complete world API reference is in [`docs/world-apis.md`](docs/world-apis.md).

## Event implementation

LuaCS acquires the live `IGameEventManager2` interface from the Source 2 engine factory and installs pre/post `FireEvent` hooks on that real interface. It does not require an RTTI scan to discover a `CGameEventManager` vtable.

Required CS2 function signatures are validated individually. Startup errors name every unresolved signature instead of returning one combined, ambiguous error.

## Validation

The smoke test validates:

- a clean MSVC x64 build with warnings treated as errors;
- all 17 native DLLs in the package;
- direct live game-event interface wiring;
- registration of the server-console `lua` command;
- every documented plugin lifecycle command and metadata field;
- SMG compilation, incremental caching, corruption recovery, and syntax-error preservation;
- real Source 2 `Ray_t`, schema-property, trace, and schema-native grenade adapters.

CI does not run a Windows CS2 dedicated server. Live engine behavior must still be tested on a current server and loaded map; unavailable signatures, fields, entities, recipients, or rules produce explicit errors.
