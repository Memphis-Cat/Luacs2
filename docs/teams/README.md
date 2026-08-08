# `cs2.teams`

`cs2.teams` handles team membership, team player lists, and team scores.

```lua
local teams = require("cs2.teams")
```

## Constants

```text
teams.NONE = 0
teams.SPECTATOR = 1
teams.T = 2
teams.TERRORIST = 2
teams.CT = 3
teams.COUNTER_TERRORIST = 3
```

## Change a player's team

```text
teams.change(player, team)
teams.switch(player, team)
```

Both accept a player table or slot. The requested team must be spectator, T, or CT for a team change.

## Get players on a team

```lua
local terrorists = teams.get_players(teams.T)
for _, player in ipairs(terrorists) do
    print(player.name)
end
```

The returned player tables include live fields such as `team`, `health`, `armor`, `alive`, and `pawn_index` when available.

## Scores

```text
teams.get_score(team)
teams.set_score(team, score)
teams.add_score(team, delta)
```

Score operations accept spectator/T/CT IDs where supported by the native service, but normal score use is T or CT.

Scores cannot become negative or exceed a signed 32-bit integer. `add_score` checks the final value before writing it.

```lua
local ct_score = teams.get_score(teams.CT)
local new_score = teams.add_score(teams.CT, 1)
```

Runtime failures return `nil, error_message`.