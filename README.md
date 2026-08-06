# LuaCS

LuaCS is a Windows x64 Metamod:Source runtime that embeds Lua 5.5.1 for Counter-Strike 2 server plugins.

Version `0.3.0-player-events-inventory` completes the three engine-facing areas covered by this branch:

- live player/controller/pawn state and actions;
- real CS2 game-event pre/post hooks with typed access, mutation, cancellation, and `dontBroadcast` control;
- live weapon and inventory enumeration, lookup, mutation, removal, dropping, switching, and ammo access.

LuaCS still does not claim to be a complete CounterStrikeSharp replacement. Arbitrary entity reflection, traces, sounds, round-control helpers, grenades, menus, and every generated CS2 schema class are outside this branch. Unsupported operations return an error instead of pretending to work.

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

Build without deploying:

```bat
build.bat --no-deploy
```

Deploy to another server:

```bat
build.bat "D:\servers\cs2\game\csgo"
```

The deploy step only writes `addons\LuaCS` and `addons\metamod\luacs2.vdf`. It does not replace Metamod itself.

## Compile Lua plugins

Put single-file `.lua` plugins beside `compile.exe` and run it, or drag one or more Lua files onto it.

The compiler:

- parses with Lua 5.5.1;
- emits stripped bytecode;
- authenticates and encrypts `.smg` packages with AES-256-GCM;
- records the source SHA-256;
- skips unchanged sources;
- preserves the previous output after a syntax failure;
- reports exact deprecated names from the supplied API snapshot.

On first use it creates `config\luacs.key`. Keep that key: the runtime requires the same key to load the compiled `.smg` files. Encryption obstructs casual extraction, but code cannot be made unrecoverable from a machine that possesses both the runtime and key.

## Module loading

Core globals are always available:

```lua
print("hello")
local position = Vector(1, 2, 3)
local tint = Color(255, 80, 20, 255)
```

Modules can be loaded through either namespace:

```lua
local players = require("cs2.players")
local same_players = require("cs2").players
assert(players == same_players)
```

Available native modules:

```text
events, timers, players, commands, math, weapons, hud, cvars
```

## Events

### Lifecycle events

LuaCS emits these runtime lifecycle events:

```text
player_connect
player_activate
player_put_in_server
player_disconnect
client_command
map_start
map_end
game_frame
```

```lua
local events = require("cs2.events")

events.on("player_connect", function(event)
    print(event.player.name, event.player.steam64)
end)

events.Instance:OnPlayerActivate(function(player)
    print(player.name, "is active")
end)
```

Lifecycle events are normal Lua tables. They are not mutable `IGameEvent` objects and therefore do not support typed engine-event getters/setters or cancellation.

### Real CS2 game events

Any real CS2 event name can be subscribed in pre or post phase:

```lua
local events = require("cs2.events")

local pre_id = events.on("player_hurt", function(event)
    local victim = event:get_player("userid")
    print(victim and victim.name, event:get_int("dmg_health"))
end)

local post_id = events.on_post("round_start", function(event)
    print("round_start finished", event.id)
end)

events.off(pre_id)
events.off(post_id)
```

Alias registration is also available:

```lua
events.Instance:OnPlayerDeath(function(event)
    print(event:get_string("weapon"))
end)

events.Instance:OnPostRoundStart(function(event)
    print("post round_start")
end)
```

A real event object exposes these fields:

```text
name
id
reliable
local
post
dont_broadcast
```

Typed read methods:

```text
has_key(key)
is_empty(key)
get_bool(key [, fallback])
get_int(key [, fallback])
get_uint64(key [, fallback])
get_float(key [, fallback])
get_string(key [, fallback])
get_player_slot(key)
get_entity_index(key)
get_pawn_index(key)
get_player(key)
```

Typed mutation methods:

```text
set_bool(key, value)
set_int(key, value)
set_uint64(key, value)
set_float(key, value)
set_string(key, value)
set_dont_broadcast(value)
cancel()
```

Mutation, `set_dont_broadcast`, and `cancel` are pre-hook operations. Post hooks receive a duplicated event after CS2 fires the original, so Lua never retains a pointer to engine-owned freed memory. Event objects are valid only during their callback.

Example cancellation:

```lua
events.on("player_death", function(event)
    local victim = event:get_player("userid")
    if victim and victim.name == "ProtectedPlayer" then
        event:cancel()
    end
end)
```

## Players

Player functions accept either an integer slot or a player table where appropriate.

### Lookup

```lua
local players = require("cs2.players")

local by_slot = players.get_by_slot(0)
local same = players.get(0)
local by_steam64 = players.get(76561198000000000)
local by_exact_name = players.get("Player Name")
local by_steam2 = players.get("STEAM_1:0:12345")
local everyone = players.all()
```

Lookup uses connected-player metadata maintained by the public server hooks. `players.get` performs exact matching; it does not silently choose a partial-name match.

### Metadata fields

```text
slot
name
steam64
steamid
fake
connected
active
```

### Live controller/pawn fields

Call `player:refresh()` to update an existing player table from the current controller and pawn:

```text
valid
has_controller
has_pawn
alive
controller_index
pawn_index
pawn_handle
health
max_health
armor
team
money
ping
helmet
defuser
on_ground
position
velocity
eye_angles
```

Vectors contain `x`, `y`, and `z`.

```lua
local player = players.get_by_slot(0)
if player then
    player:refresh()
    print(player.health, player.armor, player.position.x)
end
```

### Player actions

Every action is available as a module function and as a player method:

```text
refresh
is_valid
is_alive
set_health
set_armor
set_money
set_helmet
set_defuser
set_prevent_weapon_pickup
teleport
kill
respawn
change_team
switch_team
```

Examples:

```lua
player:set_health(100)
player:set_armor(100)
player:set_money(16000)
player:set_helmet(true)
player:set_defuser(true)
player:set_prevent_weapon_pickup(false)

player:teleport(
    Vector(100, 200, 300), -- position; nil keeps current
    Vector(0, 90, 0),      -- angles; nil keeps current
    Vector(0, 0, 0)        -- velocity; nil keeps current
)

player:kill(false) -- true requests exploding suicide
player:respawn()
player:change_team(players.TEAM_CT) -- follows normal team-change rules
player:switch_team(players.TEAM_T)  -- forced live switch
```

Team constants:

```text
TEAM_NONE       = 0
TEAM_SPECTATOR  = 1
TEAM_T          = 2
TEAM_CT         = 3
```

Failed actions return `nil, error_message`. Successful actions return `true`, except `refresh`, which returns the refreshed player table.

## Weapons and inventory

Weapon operations accept a player table or slot. Item names are exact CS2 classnames; LuaCS does not invent aliases.

### Weapon fields

```text
valid
entity_index
handle
owner_slot
active
classname
clip1
clip2
reserve1
reserve2
```

### Inventory and lookup

```lua
local weapons = require("cs2.weapons")

local inventory = weapons.list(player)
local amount = weapons.count(player)
local active = weapons.active(player)
local ak = weapons.find(player, "weapon_ak47")
local has_ak = weapons.has(player, "weapon_ak47")
local by_entity_index = weapons.get(ak.entity_index)
```

`weapons.find` uses an exact case-insensitive classname match.

### Give, remove, drop, and switch

```lua
local weapon, error_message = weapons.give(player, "weapon_ak47")
if not weapon then
    print(error_message)
    return
end

weapon:switch()
weapon:drop()
weapon:remove()       -- removes and deletes the entity
weapon:remove(false)  -- removes from inventory without deleting the entity
```

Module forms:

```text
give(player_or_slot, classname)
list(player_or_slot)
count(player_or_slot)
get(weapon_or_entity_index)
active(player_or_slot)
find(player_or_slot, classname)
has(player_or_slot, classname)
refresh(weapon_or_entity_index)
remove(player_or_slot, weapon_or_entity_index [, delete_entity=true])
remove_by_classname(player_or_slot, classname [, delete_entity=true])
remove_all(player_or_slot)
drop(player_or_slot, weapon_or_entity_index)
drop_active(player_or_slot)
switch(player_or_slot, weapon_or_entity_index)
```

Weapon-object methods:

```text
refresh()
set_clip1(value)
set_clip2(value)
set_reserve1(value)
set_reserve2(value)
remove([delete_entity=true])
drop()
switch()
```

### Clip, reserve, and global ammo

```lua
weapon:set_clip1(30)
weapon:set_clip2(-1)
weapon:set_reserve1(90)
weapon:set_reserve2(0)

-- Player weapon-services ammo array: indexes 0 through 31.
local amount = weapons.get_ammo(player, 2)
weapons.set_ammo(player, 2, 120)
```

Clips allow `-1` for weapons that do not use that clip. Reserve values must be non-negative. Global ammo values must be between `0` and `65535`.

All inventory mutations validate that the selected weapon belongs to the supplied player. Failed operations return `nil, error_message` rather than acting on an unrelated entity.

## HUD

```lua
local hud = require("cs2.hud")

hud.chat(player, "Hello in chat")
hud.center(player, "Center message")
hud.alert_all("Message for everyone")
hud.print(player, hud.CONSOLE, "Console message")
hud.broadcast(hud.NOTIFY, "Notification for everyone")
```

Destinations:

```text
hud.NOTIFY
hud.CONSOLE
hud.CHAT
hud.CENTER
hud.ALERT
```

Convenience functions:

```text
notify, console, chat, center, alert
notify_all, console_all, chat_all, center_all, alert_all
```

HUD output uses CS2's verified `ClientPrint` and `UTIL_ClientPrintAll` functions from the packaged Windows gamedata.

## Cvars

```lua
local cvars = require("cs2.cvars")

if cvars.exists("mp_roundtime") then
    print(cvars.get_number("mp_roundtime"))
    local ok, error_message = cvars.set("mp_roundtime", 10)
    if not ok then print(error_message) end
end
```

Functions:

```text
exists
get
get_string
set
set_string
get_bool
get_int
get_number
```

Reads and writes use the real Source 2 `ICvar`/`ConVarRefAbstract` interface and return `nil, error_message` when the cvar is missing or conversion fails.

## Commands

Commands are matched from client console commands and `!command`/`/command` chat text:

```lua
local commands = require("cs2.commands")

commands.on("hello", function(player, arguments, raw)
    print(player and player.name or "console", arguments, raw)
end)
```

## Timers

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

## Logging

Each compiled plugin gets a numeric log filename in `logs`. `print(...)` writes to that file, the LuaCS core log, and the server console.

## Reference data and validation

`gamedata/reference` preserves the supplied Valve `cs_script` API snapshot and the Windows gamedata reference pack. Engine-facing APIs are added only when backed by a real interface, Source 2 schema resolution, a verified function signature, or a verified virtual index.

The GitHub Actions Windows build validates:

- native MSVC compilation;
- all LuaCS native modules;
- compiler success and failure behavior;
- final package generation and artifact upload.

CI cannot prove live dedicated-server behavior. Loading the produced package on a current CS2 Windows dedicated server remains the final runtime validation for schema replication, game-event interception, and engine virtual calls.
