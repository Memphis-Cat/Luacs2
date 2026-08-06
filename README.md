# LuaCS

LuaCS is a Windows x64 Metamod:Source runtime that embeds Lua 5.5.1 for Counter-Strike 2 server plugins.

Version `0.4.0-teams-entities-sounds` includes:

- isolated Lua plugin states and authenticated encrypted SMG packages;
- timers, commands, lifecycle events, and real mutable CS2 pre/post game events;
- live player/controller/pawn state and actions;
- weapons, inventory, clips, reserve ammo, and player ammo access;
- HUD output and Source 2 cvars;
- team membership, player lists, and team score control;
- round state, restart, termination, freeze, and unfreeze control;
- arbitrary entity discovery, creation, spawning, transforms, ownership, parenting, inputs, and removal;
- spatial and filtered sound emission with real SOS GUID stopping and logical channels.

Unsupported operations return explicit errors instead of pretending to work. This branch does not claim to expose every generated CS2 schema class or every CounterStrikeSharp feature. Traces, grenade helpers, menus, and complete live-server validation remain separate work.

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
    │   └── sounds.dll
    ├── scripting/
    │   ├── compile.exe
    │   └── example_welcome.lua
    ├── plugins/
    ├── gamedata/reference/
    ├── config/
    └── logs/
```

## Build

Run from an x64 Visual Studio Developer Command Prompt:

```bat
build.bat --no-deploy
```

Build and deploy to the default server:

```bat
build.bat
```

Deploy to another CS2 `game\csgo` directory:

```bat
build.bat "D:\servers\cs2\game\csgo"
```

The build fetches pinned public Metamod, AlliedModders HL2SDK CS2, and SteamTracking protobuf revisions. Pinned source defects that would produce warnings are corrected deterministically before compilation. MSVC uses `/WX`, so a compiler warning fails the build rather than being hidden.

## Compile Lua plugins

Put single-file `.lua` plugins beside `compile.exe` and run it, or drag Lua files onto it.

The compiler:

- parses with Lua 5.5.1;
- emits stripped bytecode;
- authenticates and encrypts `.smg` packages with AES-256-GCM;
- records the source SHA-256;
- skips unchanged sources;
- preserves the previous output after a syntax failure;
- reports exact deprecated names from the supplied API snapshot.

On first use it creates `config\luacs.key`. Keep the same key with the compiled `.smg` files.

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
```

## Teams and rounds

```lua
teams.change(player, teams.CT)
local terrorists = teams.get_players(teams.T)
teams.set_score(teams.CT, 5)

rounds.restart(1.0)
rounds.terminate(rounds.CT_WIN)
rounds.freeze()
rounds.unfreeze()
print(rounds.get_number())
```

## Entities

```lua
local doors = entities.find_by_classname("func_door*")
local named = entities.find_by_name("door_1")

local prop, err = entities.create("prop_dynamic", {
    position = Vector(100, 200, 300),
    angles = Vector(0, 90, 0),
    spawn = true,
})

if prop then
    prop:set_parent(named)
    prop:input("Enable")
    prop:remove()
end
```

## Sounds

```lua
local sound, err = sounds.emit(player, "sounds/example.vsnd", {
    volume = 0.8,
    pitch = 100,
    channel = 4,
    origin = Vector(100, 200, 300),
})

if sound then sound:stop() end
sounds.emit_all("sounds/example.vsnd")
sounds.stop_channel(4)
```

The complete teams, rounds, entities, and sounds reference is in [`docs/world-apis.md`](docs/world-apis.md).

## Engine implementation

LuaCS uses current Source 2 interfaces, dynamically resolved schema fields, and packaged Windows gamedata. The world APIs use real engine behavior:

- `CTeam::m_iScore` for scores;
- `CCSGameRules` state and `CCSGameRules_TerminateRound` for rounds;
- `UTIL_CreateEntityByName`, `CBaseEntity_DispatchSpawn`, `CBaseEntity::Teleport`, entity I/O, and `UTIL_Remove` for entities;
- CS2 sound-event emission with the returned SOS GUID and `CMsgSosStopSoundEvent` for stopping.

## Validation

GitHub Actions validates:

- a clean MSVC x64 build with warnings treated as errors;
- all 14 native DLLs in the package;
- SMG compilation and incremental caching;
- corrupted-package recovery;
- syntax-error rejection without destroying the previous compiled package;
- successful artifact creation.

CI does not run a Windows CS2 dedicated server. Live engine behavior must still be exercised on a current server and loaded map; unavailable interfaces, signatures, fields, entities, recipients, or rules produce explicit Lua errors.
