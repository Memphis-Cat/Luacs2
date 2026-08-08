# `cs2.rounds`

`cs2.rounds` exposes current round state and round-control operations.

```lua
local rounds = require("cs2.rounds")
```

## State

```text
rounds.state()
rounds.get()       -- alias of state
rounds.get_number()
rounds.is_frozen()
```

`rounds.state()` returns:

```text
valid
frozen
number
win_status
win_reason
restart_time
```

`restart_time` is validated as a finite value before Lua receives it.

## Restart

```lua
rounds.restart()      -- defaults to 1 second
rounds.restart(3.0)
```

Delay must be finite and between `0` and `3600` seconds.

## Terminate a round

```lua
rounds.terminate(rounds.CT_WIN, 0.5)
```

Signature:

```text
rounds.terminate(reason [, delay=0])
```

The reason must fit one byte. Delay must be finite and within `0..3600` seconds.

## Freeze control

```text
rounds.freeze()
rounds.unfreeze()
```

## Round-end reason constants

```text
UNKNOWN = 0
TARGET_BOMBED = 1
TERRORISTS_ESCAPED = 4
CTS_PREVENT_ESCAPE = 5
ESCAPING_TERRORISTS_NEUTRALIZED = 6
BOMB_DEFUSED = 7
CT_WIN / CTS_WIN = 8
T_WIN / TERRORISTS_WIN = 9
DRAW / ROUND_DRAW = 10
ALL_HOSTAGES_RESCUED = 11
TARGET_SAVED = 12
HOSTAGES_NOT_RESCUED = 13
TERRORISTS_NOT_ESCAPED = 14
GAME_COMMENCING = 16
TERRORISTS_SURRENDER = 17
CTS_SURRENDER = 18
TERRORISTS_PLANTED = 19
CTS_REACHED_HOSTAGE = 20
SURVIVAL_WIN = 21
SURVIVAL_DRAW = 22
```

Round operations that Source 2 rejects return `nil, error_message`.