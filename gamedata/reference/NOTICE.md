# Reference-data notice

These JSON files were supplied for LuaCS development on 2026-08-05.

- `cs2_cs_script_api_2026-08-03.json` is restored byte-for-byte from the verified archive chunks by `tools/restore-reference.ps1`; it is a generated searchable snapshot of Valve's public `cs_script/point_script` declaration file.
- The other JSON files came from the supplied Windows gamedata reference pack and retain their own source metadata in `sources.json`.

They are retained as reference inputs. Their presence does not mean every symbol or signature is implemented by LuaCS, current, safe to call, or available through Metamod.

The repository MIT license covers LuaCS-authored code. It does not relicense third-party declarations, signatures, generated reference data, or their underlying source material; those remain subject to their original terms.

The canonical API JSON is stored as four ordered gzip chunks because the GitHub connector used to create this branch cannot safely accept a single 285 KB text blob. The restore script verifies every chunk and the final JSON with SHA-256; no API rows are omitted.
