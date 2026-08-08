# SMG package format

`.smg` is LuaCS's authenticated plugin container. BETA TESTING 1.0 still uses SMG format version 1.

The release version and SMG format version are separate. `BETA TESTING 1.0` describes LuaCS; `version = 1` in the file header describes the binary container layout.

## Purpose

`compile.exe` parses Lua source with Lua 5.5.1, serializes stripped Lua bytecode, and writes an authenticated encrypted package. The runtime loads `.smg` packages from `addons/LuaCS/plugins`.

The format provides integrity/authentication and keeps plaintext source out of the deployed package. It is not described as impossible-to-reverse DRM.

## Header

Fields are packed in this order:

```text
char[8]   magic = "LUACSMG\0"
uint32    version = 1
uint32    flags = 1
byte[32]  SHA-256 of the original Lua source
byte[12]  AES-GCM nonce
byte[16]  AES-GCM authentication tag
uint64    plaintext bytecode size
uint64    encrypted payload size
byte[]    encrypted stripped Lua bytecode
```

`flags = 1` currently means AES-256-GCM.

## Authentication

The header is authenticated as AES-GCM additional data with the tag field treated as zero while calculating/verifying the tag.

Changing the authenticated header, nonce, tag, or ciphertext causes authentication to fail.

The source SHA-256 is also used by the compiler's incremental build logic. If the source hash still matches a valid existing package, the compiler can report the package as already compiled.

## Key

Each LuaCS installation uses a 32-byte key stored at:

```text
addons/LuaCS/config/luacs.key
```

The compiler creates the key when needed.

On a clean installation with no `.smg` packages, the runtime may create the first key. If compiled packages already exist and the key is missing, the runtime refuses to invent a replacement because a new key could not authenticate/decrypt the existing packages.

Keep the key private with the server installation. Do not publish a production key with a release archive or repository.

## Compiler failure behavior

The compiler writes packages atomically. A failed compile or failed authenticated write should not replace the last valid `.smg` with a partial file.

Corrupt or unauthenticated packages are not treated as valid cache entries.

## Compatibility

A future change to the container layout must increment the SMG header version. LuaCS release names do not implicitly change this field.