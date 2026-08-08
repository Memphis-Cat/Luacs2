# LuaCS plugins

A LuaCS plugin is a compiled `.smg` package with one isolated Lua state.

## Metadata

Declare metadata before registering the rest of the plugin:

```lua
plugin = {
    name = "My Plugin",
    author = "Byanca",
    version = "1.0.0",
    description = "What the plugin does."
}
```

All fields are optional. Missing values fall back to the package name, `Unknown`, or `Unspecified`.

## Startup

Top-level Lua code is the startup code. If it throws an error, that package fails to load and the runtime continues loading other packages.

```lua
local events = require("cs2.events")
print("plugin loaded")
```

## Cleanup

A normal unload checks for one of these cleanup forms:

```lua
function OnUnload()
    print("cleaning up")
end
```

or:

```lua
function plugin:unload()
    print("cleaning up", self.name)
end
```

If cleanup throws an error, a normal unload or refresh is refused so the failure is visible. `force_unload` and `force_load` intentionally skip the cleanup callback.

## Runtime callback failures

An uncaught error in a loaded plugin callback disables that plugin. LuaCS releases its event callbacks, command callbacks, and timers. Other plugins remain active.

The disabled plugin is not destroyed in the middle of the failing dispatch. Reload or refresh it after fixing the source and recompiling the package.

## Server-console management

```text
lua
lua help
lua version
lua clear
lua plugins list
lua plugins info
lua plugins info <target>
lua plugins load <target>
lua plugins unload <target>
lua plugins refresh <target>
lua plugins retry
lua plugins force_load <target>
lua plugins force_unload <target>
```

`<target>` can be a `.lua` name, `.smg` name, filename stem, or a declared plugin name that LuaCS already knows.

`retry` attempts only packages whose most recent load failed.

`lua version` reports `LuaCS BETA TESTING 1.0` and the embedded Lua language version separately.

## Logs

Plugin output is stored under:

```text
addons/LuaCS/logs/
```

LuaCS also maintains a native error log for runtime/plugin-native failures. When reporting a beta issue, include the relevant error text and the CS2 server build if possible.