# `cs2.hud`

`cs2.hud` sends Source 2 text messages to one player or all connected players.

```lua
local hud = require("cs2.hud")
```

A player argument can be a player table or slot number.

## Destinations

```text
hud.NOTIFY
hud.CONSOLE
hud.CHAT
hud.CENTER
hud.ALERT
```

## Generic forms

```text
hud.print(player, destination, message)
hud.broadcast(destination, message)
```

Example:

```lua
hud.print(player, hud.CHAT, "Hello")
hud.broadcast(hud.CENTER, "Round starting")
```

## Convenience functions

One player:

```text
hud.notify(player, message)
hud.console(player, message)
hud.chat(player, message)
hud.center(player, message)
hud.alert(player, message)
```

All players:

```text
hud.notify_all(message)
hud.console_all(message)
hud.chat_all(message)
hud.center_all(message)
hud.alert_all(message)
```

Messages may not contain embedded NUL bytes. An invalid destination is rejected before the request reaches Source 2.

Successful sends return `true`. Engine failures return `nil, error_message`.