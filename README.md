# LuaCS

**BETA TESTING 1.0**

LuaCS is a Windows x64 Metamod:Source runtime that embeds Lua 5.5.1 for Counter-Strike 2 dedicated-server plugins.

BETA TESTING 1.0 includes:

- isolated Lua states and authenticated encrypted `.smg` packages;
- server-console plugin administration through the `lua` command;
- plugin name, author, version, and description metadata;
- timers, commands, lifecycle events, and mutable CS2 pre/post game events;
- live player/controller/pawn state and actions;
- weapons, inventory, clips, reserve ammo, and player ammo access;
- HUD output and Source 2 cvars;
- team, round, entity, sound, schema-property, trace, and grenade APIs.

This is a beta testing release. CS2 updates can change signatures, schema fields, or engine behavior, so server owners should expect compatibility fixes while LuaCS is still in beta.

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
```

Deploy to a CS2 `game\csgo` directory after the build succeeds:

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

`lua version` reports LuaCS as **BETA TESTING 1.0** and also reports the embedded Lua language version separately.

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

The complete world API reference is in [`docs/world-apis.md`](docs/world-apis.md). Dedicated references cover [`cs2.properties`](docs/schema-properties.md), [`cs2.traces`](docs/traces.md), and [`cs2.grenades`](docs/grenades.md).

## Weapon slots and replacement

Every weapon table returned by `cs2.weapons` includes a `slot` field. Current values are:

```text
primary
secondary
knife
grenade
equipment
c4
melee
other
```

`other` is a classification fallback and is not a replaceable slot.

Use `weapons.replace_slot(player, slot, classname, equip)` to replace all weapons in one classified slot, give the new weapon, and optionally equip it. `equip` defaults to `true`.

```lua
local weapons = require("cs2.weapons")

local awp, err = weapons.replace_slot(
    player,
    "primary",
    "weapon_awp",
    true
)

if not awp then
    print(err)
    return
end

print(awp.classname, awp.slot)
```

The requested slot must match the target classname. For example, attempting to replace `secondary` with `weapon_ak47` is rejected instead of silently removing the wrong category.

`auto` is accepted as an input mode when the target classname should select its own slot automatically:

```lua
local deagle, err = weapons.replace_slot(
    player,
    "auto",
    "weapon_deagle",
    true
)
```

`auto` is never returned by `weapon.slot`.

## Event implementation

LuaCS acquires the live `IGameEventManager2` interface from the Source 2 engine factory and installs pre/post `FireEvent` hooks on that real interface. It does not require an RTTI scan to discover a `CGameEventManager` vtable.

Required CS2 function signatures are validated individually. Startup errors name every unresolved signature instead of returning one combined, ambiguous error.

## Beta status

BETA TESTING 1.0 is the first public beta-testing release of LuaCS. It is intended for current Windows x64 CS2 dedicated servers running Metamod:Source.

LuaCS contains defensive validation around native engine access, but CS2 is a moving target. A game update can invalidate signatures, schema paths, or assumptions used by native modules. If LuaCS fails to start or a Lua plugin encounters a native/runtime error, check `addons/LuaCS/logs/luacs-errors.log` and include the relevant error when reporting the issue.
