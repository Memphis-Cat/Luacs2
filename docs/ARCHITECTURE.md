# LuaCS architecture

## Trust boundary

Lua source is compiled offline by `compile.exe`. The Metamod plugin only loads authenticated `.smg` files. Every plugin runs in its own Lua state, event registry, timer registry, native-module handle set, and log file.

## Module loading

LuaCS inserts a searcher into `package.searchers`. A request for `cs2.players` resolves only to `LuaCS/bin/players.dll`. The DLL must export:

```cpp
extern "C" __declspec(dllexport)
int LuaCS_OpenModule(lua_State* state, const luacs::Services* services);
```

The function must push exactly one Lua table. Lua's standard `require` stores that table in `package.loaded`, so repeated imports return the same object.

The service table is ABI-versioned. Native modules do not receive private `Runtime` C++ objects and do not depend on C++ class layout across DLL boundaries.

## Current public-engine integration

The Metamod plugin uses the Source 2 sample interfaces and hooks for:

- `IServerGameDLL::GameFrame`
- `IServerGameClients::OnClientConnected`
- `IServerGameClients::ClientActive`
- `IServerGameClients::ClientPutInServer`
- `IServerGameClients::ClientDisconnect`
- `IServerGameClients::ClientCommand`
- Metamod level initialization/shutdown listener callbacks

This supports genuine scheduling, lifecycle events, connected-player identity snapshots, and command dispatch without pretending that arbitrary pawn/entity operations are already available.

## Planned module boundaries

The target API remains:

```text
cs2.players
cs2.weapons
cs2.grenades
cs2.entities
cs2.events
cs2.timers
cs2.traces
cs2.rounds
cs2.teams
cs2.commands
cs2.cvars
cs2.sounds
cs2.math
```

Only modules with actual implementations are built. Missing DLLs cause `require` to fail with the searched path rather than returning placeholder tables.
