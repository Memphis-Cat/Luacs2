# LuaCS architecture

This document describes the BETA TESTING 1.0 runtime layout. It is about implementation boundaries, not the Lua API catalog. For Lua usage, start at [`README.md`](README.md).

## Process layout

LuaCS runs inside the Windows x64 CS2 dedicated-server process as a Metamod:Source plugin.

The package contains:

```text
luacs2.dll       Metamod plugin, engine hooks, Source 2 adapters
lua55.dll        embedded Lua 5.5.1 runtime
compile.exe      offline Lua -> SMG compiler
*.dll            Lua API modules such as players.dll and weapons.dll
*.smg            authenticated compiled Lua plugins
```

Lua's core is compiled as C++ so protected Lua errors unwind C++ stack objects safely. Its exported API remains C linkage so runtime/modules agree on the normal Lua ABI.

## Plugin isolation

Each loaded `.smg` gets its own `lua_State` and owns its own:

- global namespace;
- `package.loaded` cache;
- event callbacks;
- command callbacks;
- timers;
- native module handles;
- plugin metadata;
- log file.

One plugin does not receive another plugin's Lua state or registry references.

If a loaded plugin callback throws an uncaught Lua error, LuaCS marks that VM disabled and releases its event, command, and timer references. The VM is not destroyed in the middle of the failing dispatch. Other plugins continue running.

Startup failures are isolated too: one bad package does not stop the runtime from loading the next package.

## Module ABI

Native Lua modules export:

```cpp
extern "C" __declspec(dllexport)
int LuaCS_OpenModule(lua_State* state, const luacs::Services* services);
```

The module must return exactly one Lua value, normally a table.

`Services` is a versioned C-style service table. Modules use function pointers and plain ABI data structures rather than depending on the private C++ layout of `Runtime`.

Higher-level engine modules also resolve versioned `WorldServices` and `AdvancedWorldServices` tables. Their ABI versions are separate compatibility contracts and are not the LuaCS marketing/release version.

## Public Lua modules

BETA TESTING 1.0 builds these modules:

```text
cs2.events
cs2.timers
cs2.players
cs2.commands
cs2.math
cs2.weapons
cs2.hud
cs2.cvars
cs2.teams
cs2.rounds
cs2.entities
cs2.sounds
cs2.properties
cs2.traces
cs2.grenades
```

`require("cs2")` provides a lazy root table that resolves `cs2.<field>` modules on demand.

## Metamod and Source 2 hooks

The plugin installs the required server/game hooks for:

- game-frame ticking;
- client connection;
- client activation;
- client insertion into the server;
- client disconnect;
- client commands;
- game-event loading;
- game-event pre fire;
- game-event post fire;
- map lifecycle callbacks through the Metamod listener.

Chat command routing uses Source 2 `player_chat` as the authoritative chat path. `say`/`say_team` are skipped in the client-command bridge so chat callbacks are not dispatched twice.

## Game events

LuaCS captures the live `IGameEventManager2` interface from the Source 2 factory. Pre and post `FireEvent` hooks are installed on that interface.

Pre callbacks operate on the live event token and may read/write supported fields, cancel the event, or change `dont_broadcast`.

Post callbacks use a bounded event-copy pool so Lua receives a stable duplicate after Source 2 fires the event. Copy tracking is bounded instead of allowing unbounded allocation during event bursts.

## Generated Source 2 bindings

The checked-in game API source is used as the verified input for generated build sources.

During `build.bat` LuaCS:

1. restores pinned reference data;
2. generates a disk-backed signature scanner;
3. injects per-signature diagnostics;
4. generates the live server-module plugin binding;
5. generates the real `Ray_t` advanced trace layer;
6. compiles the generated files rather than the checked-in plugin/game API translation units where required.

Generation is intentionally strict. If an expected source marker drifts, generation fails instead of applying a best-effort patch to unknown code.

## Signature scanning

Signatures are searched against executable sections from the server module's disk image, then translated to the loaded module by RVA.

Before calculated live addresses are read, LuaCS checks the relevant pages with `VirtualQuery`. Guard pages, no-access pages, uncommitted memory, overflowed ranges, and unreadable cross-page ranges are rejected.

This keeps signature lookup diagnostics separate from unsafe blind pointer dereferences.

## World service boundaries

The player, weapon, team, round, entity, sound, schema, trace, and grenade adapters validate requests before calling Source 2 and validate important returned data before handing it to Lua.

Examples include:

- finite vector/angle checks;
- entity and player index ranges;
- exact-width integer checks;
- exact unsigned 64-bit conversion;
- valid handles and live entity identity;
- bounded string/raw data;
- trace fraction/result checks;
- transactional entity/grenade creation.

LuaCS returns an error for unsupported operations rather than silently emulating missing engine behavior.

## Build policy

The Windows build uses `/WX`; warnings are errors. The build also pins its external Metamod, HL2SDK, and protobuf revisions so changes in those dependencies do not silently alter a release build.