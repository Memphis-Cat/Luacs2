#include "luacs/module_api.h"

extern "C" {
#include "lauxlib.h"
}

namespace {
const luacs::Services* g_services = nullptr;

int on(lua_State* state) {
    const char* event_name = luaL_checkstring(state, 1);
    luaL_checktype(state, 2, LUA_TFUNCTION);
    if (!g_services->event_on(g_services->context, state, event_name, 2,
                              luacs::EventCallbackMode::EventTable)) {
        return luaL_error(state, "could not register event '%s'", event_name);
    }
    lua_pushvalue(state, 2);
    return 1;
}

int on_player_alias(lua_State* state) {
    const char* event_name = lua_tostring(state, lua_upvalueindex(1));
    const int callback_index = lua_gettop(state);
    luaL_checktype(state, callback_index, LUA_TFUNCTION);
    if (!g_services->event_on(g_services->context, state, event_name, callback_index,
                              luacs::EventCallbackMode::PlayerOnly)) {
        return luaL_error(state, "could not register event '%s'", event_name);
    }
    lua_pushvalue(state, callback_index);
    return 1;
}

void add_alias(lua_State* state, const char* lua_name, const char* event_name) {
    lua_pushstring(state, event_name);
    lua_pushcclosure(state, on_player_alias, 1);
    lua_setfield(state, -2, lua_name);
}
} // namespace

LUACS_MODULE_EXPORT int LuaCS_OpenModule(lua_State* state, const luacs::Services* services) {
    if (!services || services->abi_version != luacs::kModuleAbiVersion || !services->event_on) {
        return luaL_error(state, "events.dll received an incompatible LuaCS service table");
    }
    g_services = services;
    lua_createtable(state, 0, 2);
    lua_pushcfunction(state, on);
    lua_setfield(state, -2, "on");

    lua_createtable(state, 0, 8);
    add_alias(state, "OnPlayerConnect", "player_connect");
    add_alias(state, "OnPlayerActivate", "player_activate");
    add_alias(state, "OnPlayerPutInServer", "player_put_in_server");
    add_alias(state, "OnPlayerDisconnect", "player_disconnect");
    lua_setfield(state, -2, "Instance");
    return 1;
}
