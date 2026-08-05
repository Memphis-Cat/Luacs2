#include "luacs/module_api.h"

extern "C" {
#include "lauxlib.h"
}

namespace {
const luacs::Services* g_services = nullptr;

int add(lua_State* state, bool repeat) {
    const double delay = luaL_checknumber(state, 1);
    luaL_checktype(state, 2, LUA_TFUNCTION);
    if (delay < 0.0) return luaL_error(state, "timer delay cannot be negative");
    const auto id = g_services->timer_add(g_services->context, state, delay, repeat, 2);
    if (!id) return luaL_error(state, "could not create timer");
    lua_pushinteger(state, static_cast<lua_Integer>(id));
    return 1;
}
int after(lua_State* state) { return add(state, false); }
int every(lua_State* state) { return add(state, true); }
int cancel(lua_State* state) {
    const auto id = static_cast<std::uint64_t>(luaL_checkinteger(state, 1));
    lua_pushboolean(state, g_services->timer_cancel(g_services->context, state, id));
    return 1;
}
int now(lua_State* state) {
    lua_pushnumber(state, g_services->now(g_services->context));
    return 1;
}
} // namespace

LUACS_MODULE_EXPORT int LuaCS_OpenModule(lua_State* state, const luacs::Services* services) {
    if (!services || services->abi_version != luacs::kModuleAbiVersion ||
        !services->timer_add || !services->timer_cancel || !services->now) {
        return luaL_error(state, "timers.dll received an incompatible LuaCS service table");
    }
    g_services = services;
    lua_createtable(state, 0, 4);
    lua_pushcfunction(state, after); lua_setfield(state, -2, "after");
    lua_pushcfunction(state, every); lua_setfield(state, -2, "every");
    lua_pushcfunction(state, cancel); lua_setfield(state, -2, "cancel");
    lua_pushcfunction(state, now); lua_setfield(state, -2, "now");
    return 1;
}
