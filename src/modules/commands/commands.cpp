#include "luacs/module_api.h"

extern "C" {
#include "lauxlib.h"
}

namespace {
const luacs::Services* g_services = nullptr;
int on(lua_State* state) {
    const char* command = luaL_checkstring(state, 1);
    luaL_checktype(state, 2, LUA_TFUNCTION);
    if (!g_services->command_on(g_services->context, state, command, 2)) {
        return luaL_error(state, "could not register command '%s'", command);
    }
    lua_pushvalue(state, 2);
    return 1;
}
} // namespace

LUACS_MODULE_EXPORT int LuaCS_OpenModule(lua_State* state, const luacs::Services* services) {
    if (!services || services->abi_version != luacs::kModuleAbiVersion || !services->command_on) {
        return luaL_error(state, "commands.dll received an incompatible LuaCS service table");
    }
    g_services = services;
    lua_createtable(state, 0, 1);
    lua_pushcfunction(state, on); lua_setfield(state, -2, "on");
    return 1;
}
