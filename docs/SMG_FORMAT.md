# SMG version 1

`.smg` is a LuaCS-specific container around stripped Lua 5.5 bytecode.

Packed header fields, in order:

```text
char[8]   magic = "LUACSMG\0"
uint32    version = 1
uint32    flags = 1 (AES-256-GCM)
byte[32]  SHA-256 of the original Lua source
byte[12]  random GCM nonce
byte[16]  GCM authentication tag
uint64    plaintext bytecode size
uint64    encrypted payload size
byte[]    encrypted stripped Lua bytecode
```

The header with a zeroed tag is authenticated as AES-GCM additional data. A modified header, nonce, tag, or payload fails authentication.

The 32-byte installation key is stored at `LuaCS/config/luacs.key`. The compiler creates it. On a clean installation with no `.smg` files, the runtime may also create the first key. When compiled plugins already exist, the runtime refuses to invent a replacement because a different key would make them unreadable.

SMG is an obstruction and integrity format, not an impossible-to-reverse DRM claim. An administrator with the runtime process and key can recover bytecode.
