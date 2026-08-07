# LuaCS live API test pack

This pack validates LuaCS inside a real running CS2 server. It complements the static/build smoke suites; it does not replace them.

## Files

- `apitest.lua` - safe interactive controller. Does not intentionally kill players, restart rounds, change teams, spawn grenades, emit sounds, or crash itself.
- `apitest_survivor.lua` - neighboring plugin used to prove isolation.
- `apitest_callback_crash.lua` - intentionally throws from `!test_boom` and should quarantine only itself.
- `apitest_startup_crash.lua` - intentionally throws at startup and should fail without blocking other plugins.
- `apitest_unload_crash.lua` - intentionally throws from `plugin:unload()` so normal unload should be refused; force unload should still remove it.
- `install-live-tests.bat` - copies/compiles the controller, or all isolation packages when `isolation` is supplied.
- `remove-live-tests.bat` - removes all live-test `.lua` and `.smg` files from the target LuaCS installation.

## Install

The default game root is the current LuaCS development server:

```bat
tests\live\install-live-tests.bat
```

For another server:

```bat
tests\live\install-live-tests.bat "D:\path\to\Counter-Strike Global Offensive\game\csgo"
```

The installer uses the target server's own `addons\LuaCS\scripting\compile.exe`, so the generated SMG is authenticated with that LuaCS installation's key.

Then in the CS2 server console:

```text
lua plugins load apitest
```

If it is already loaded:

```text
lua plugins refresh apitest
```

## Main commands

Run these from an in-game player's chat:

```text
!lua_test help
!lua_test all
!lua_test modules
!lua_test players
!lua_test weapons
!lua_test ammo 0
!lua_test timers
!lua_test cvars
!lua_test rounds
!lua_test teams
!lua_test entities
!lua_test properties
!lua_test traces
!lua_test grenades
!lua_test types
!lua_test events
!lua_test death
!lua_test unsupported
!lua_test summary
!lua_test reset
```

`/lua_test ...` works through the same chat-command bridge.

### Ammo and magazines

`!lua_test weapons` validates:

- active-weapon discovery;
- `clip1` magazine read;
- `reserve1` read;
- `weapon:set_clip1()` by writing the existing value back;
- `weapon:set_reserve1()` by writing the existing value back;
- `weapons.get_ammo()` / `weapons.set_ammo()` using an ammo-array index.

The same-value writes are intentional: they prove the mutation path without changing the player's actual magazine/reserve count.

Use another ammo-array index with:

```text
!lua_test ammo 2
```

Valid ammo-array indexes are `0..31`.

### Death event

The controller always watches `player_death`. After a real kill, it records:

- victim;
- attacker when available;
- weapon;
- headshot flag;
- event count.

Then run:

```text
!lua_test death
```

Test normal kills, headshots, suicides/world kills, grenade kills, bot kills, and disconnect races manually.

### Event coverage

The controller passively counts these events when they occur:

```text
player_connect
player_activate
player_put_in_server
player_disconnect
player_spawn
player_death
player_hurt
weapon_fire
weapon_reload
item_pickup
round_start
round_end
round_freeze_end
bomb_planted
bomb_defused
bomb_exploded
bomb_beginplant
bomb_abortplant
bomb_begindefuse
bomb_abortdefuse
player_team
player_jump
player_blind
grenade_thrown
hegrenade_detonate
flashbang_detonate
smokegrenade_detonate
molotov_detonate
inferno_startburn
inferno_expire
map_start
map_end
```

Run `!lua_test events` after performing gameplay actions. An event that has not happened yet is not automatically a failure.

## Isolation tests

Install the dangerous/intentional-failure packages only when wanted:

```bat
tests\live\install-live-tests.bat "F:\steamcmd2\steamapps\common\Counter-Strike Global Offensive\game\csgo" isolation
```

### Callback quarantine

Server console:

```text
lua plugins load apitest_survivor
lua plugins load apitest_callback_crash
```

Player chat:

```text
!survivor
!test_boom
!survivor
```

Expected result:

1. First `!survivor` works.
2. `!test_boom` logs the intentional Lua error and disables only `apitest_callback_crash`.
3. The second `!survivor` still works.
4. `!boom_status` should not execute after the crash plugin is quarantined.

### Startup isolation

Server console:

```text
lua plugins load apitest_startup_crash
```

Expected: that plugin fails to load, while `apitest` and `apitest_survivor` keep working.

### Unload failure

Server console:

```text
lua plugins load apitest_unload_crash
lua plugins unload apitest_unload_crash
```

Expected: normal unload is refused because the unload callback throws. Then clean it up with:

```text
lua plugins force_unload apitest_unload_crash
```

## Reload stress test

Run this repeatedly in the server console:

```text
lua plugins refresh apitest
```

Do it 10 times. Then trigger one `player_death`, one `player_spawn`, and run one `!lua_test timers`.

Expected: one callback per registered action, no multiplied listeners, and no old timers/commands continuing from previous VMs.

## Map-change test

With `apitest` loaded:

```text
changelevel de_dust2
```

Then verify:

- the plugin remains correctly loaded/reloaded according to LuaCS lifecycle policy;
- commands are not duplicated;
- old entity references are not reused;
- previous-map timers do not produce stale-object crashes;
- `map_end` / `map_start` ordering is sensible;
- event counters continue without duplicated callbacks.

## Disconnect race test

Use normal gameplay plus timers/events to disconnect a player while callbacks are pending. The key requirement is that stale player/entity references return a Lua-level failure/nil or become invalid; CS2 must not crash.

The main controller intentionally avoids keeping a disconnected player object alive for destructive operations. Dedicated race plugins can be added after this baseline is proven.

## Current intentional SKIPs

The controller reports these as `SKIP`, not `FAIL`, because LuaCS does not currently expose them as public Lua APIs:

- `Server.NextFrame`;
- `Hooks.OnGameFrame`;
- arbitrary Source 2 pre/post function hooks such as `CCSPlayerPawn::TakeDamage`;
- ConVar changed callbacks;
- an instruction-count/time watchdog for infinite Lua loops.

Do **not** run `while true do end` on the live server yet. LuaCS currently quarantines uncaught Lua errors, but an infinite loop does not throw and can occupy the CS2 server thread indefinitely.

## PASS / FAIL / SKIP meaning

- `PASS` - the live operation completed and its returned state passed validation.
- `FAIL` - an implemented API behaved unexpectedly or threw during the test.
- `SKIP` - the prerequisite did not exist (for example no active weapon/no death yet) or the public API is intentionally not implemented.

The summary is cumulative until `!lua_test reset` or plugin reload.

## Remove

```bat
tests\live\remove-live-tests.bat
```

or pass another game root as the first argument.
