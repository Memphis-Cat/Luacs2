# LuaCS compiler

LuaCS BETA TESTING 1.0 loads authenticated `.smg` packages, not loose Lua files. `compile.exe` turns plugin source into those packages.

## Where it lives

After a build or installation:

```text
addons/LuaCS/scripting/compile.exe
```

Place `.lua` files beside it or pass file paths on the command line.

## Compile every Lua file beside the compiler

```bat
compile.exe
```

## Compile selected files

```bat
compile.exe myplugin.lua admin.lua
```

The extension check is case-insensitive. Requested files must resolve to regular `.lua` files.

## Output

Compiled packages are written to:

```text
addons/LuaCS/plugins/<name>.smg
```

The compiler keeps a source hash in the authenticated package. If the source has not changed and the existing package is valid, the compiler reports it as already compiled instead of rebuilding it.

If compilation fails, LuaCS preserves the previous valid `.smg` instead of replacing it with a broken package.

## Key file

The first compilation creates:

```text
addons/LuaCS/config/luacs.key
```

Keep this file with the installation. Existing `.smg` packages are authenticated and encrypted with that key. Replacing the key makes those packages unreadable.

Do not commit a real server key to a public repository.

## Preflight checks

Before bytecode is written, the compiler rejects problems such as:

- unreadable or missing source files;
- invalid UTF-8;
- embedded NUL bytes;
- syntax/parser errors;
- literal `require("cs2.*")` names that do not match a LuaCS module;
- serialization or authenticated-write failures.

Warnings for known deprecated symbols are still reported on cached source so a cached package does not hide relevant migration information.

## Packaging format

See [`../SMG_FORMAT.md`](../SMG_FORMAT.md) for the byte-level `.smg` format.