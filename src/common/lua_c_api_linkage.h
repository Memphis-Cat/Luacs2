#pragma once

// Lua is compiled as C++ so its protected-error path unwinds C++ frames with
// exceptions instead of longjmp. LuaCS modules, however, intentionally consume
// the stable C Lua ABI. Force the public Lua declarations to C linkage before
// each vendored Lua translation unit is compiled so the implementation exports
// the same undecorated symbols that modules and compile.exe import.
#if defined(LUA_BUILD_AS_DLL)
#  if !defined(LUA_CORE)
#    define LUACS_TEMP_LUA_CORE
#    define LUA_CORE
#  endif
#  if !defined(LUA_LIB)
#    define LUACS_TEMP_LUA_LIB
#    define LUA_LIB
#  endif
#endif

extern "C" {
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
}

#if defined(LUACS_TEMP_LUA_LIB)
#  undef LUA_LIB
#  undef LUACS_TEMP_LUA_LIB
#endif
#if defined(LUACS_TEMP_LUA_CORE)
#  undef LUA_CORE
#  undef LUACS_TEMP_LUA_CORE
#endif
