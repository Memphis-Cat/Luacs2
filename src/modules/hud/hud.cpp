#include "luacs/module_api.h"

extern "C" {
#include "lauxlib.h"
}

namespace {

const luacs::Services* g_services = nullptr;

int check_slot(lua_State* state, int index) {
    if (lua_isinteger(state, index)) {
        return static_cast<int>(lua_tointeger(state, index));
    }
    if (lua_istable(state, index)) {
        lua_getfield(state, index, "slot");
        if (!lua_isinteger(state, -1)) {
            lua_pop(state, 1);
            return luaL_argerror(state, index,
                                 "expected a player table containing an integer slot");
        }
        const int slot = static_cast<int>(lua_tointeger(state, -1));
        lua_pop(state, 1);
        return slot;
    }
    return luaL_argerror(state, index, "expected a player table or integer slot");
}

int push_result(lua_State* state, bool result, const char* error) {
    if (result) {
        lua_pushboolean(state, 1);
        return 1;
    }
    lua_pushnil(state);
    lua_pushstring(state, error && error[0] ? error : "CS2 rejected the HUD message");
    return 2;
}

int send(lua_State* state, int slot, int destination, const char* message) {
    char error[512]{};
    return push_result(state,
                       g_services->hud_print(g_services->context, slot, destination,
                                             message, error, sizeof(error)),
                       error);
}

int print_to(lua_State* state) {
    const int slot = check_slot(state, 1);
    const int destination = static_cast<int>(luaL_checkinteger(state, 2));
    const char* message = luaL_checkstring(state, 3);
    return send(state, slot, destination, message);
}

int broadcast(lua_State* state) {
    const int destination = static_cast<int>(luaL_checkinteger(state, 1));
    const char* message = luaL_checkstring(state, 2);
    return send(state, -1, destination, message);
}

#define LUACS_HUD_SINGLE(name, destination)                   \
    int name(lua_State* state) {                              \
        const int slot = check_slot(state, 1);                \
        return send(state, slot, destination,                 \
                    luaL_checkstring(state, 2));              \
    }

#define LUACS_HUD_ALL(name, destination)                      \
    int name(lua_State* state) {                              \
        return send(state, -1, destination,                   \
                    luaL_checkstring(state, 1));              \
    }

LUACS_HUD_SINGLE(notify, static_cast<int>(luacs::HudDestination::Notify))
LUACS_HUD_SINGLE(console, static_cast<int>(luacs::HudDestination::Console))
LUACS_HUD_SINGLE(chat, static_cast<int>(luacs::HudDestination::Chat))
LUACS_HUD_SINGLE(center, static_cast<int>(luacs::HudDestination::Center))
LUACS_HUD_SINGLE(alert, static_cast<int>(luacs::HudDestination::Alert))
LUACS_HUD_ALL(notify_all, static_cast<int>(luacs::HudDestination::Notify))
LUACS_HUD_ALL(console_all, static_cast<int>(luacs::HudDestination::Console))
LUACS_HUD_ALL(chat_all, static_cast<int>(luacs::HudDestination::Chat))
LUACS_HUD_ALL(center_all, static_cast<int>(luacs::HudDestination::Center))
LUACS_HUD_ALL(alert_all, static_cast<int>(luacs::HudDestination::Alert))

#undef LUACS_HUD_SINGLE
#undef LUACS_HUD_ALL

void add_function(lua_State* state, const char* name, lua_CFunction function) {
    lua_pushcfunction(state, function);
    lua_setfield(state, -2, name);
}

void add_integer(lua_State* state, const char* name, int value) {
    lua_pushinteger(state, value);
    lua_setfield(state, -2, name);
}

} // namespace

LUACS_MODULE_EXPORT int LuaCS_OpenModule(lua_State* state,
                                         const luacs::Services* services) {
    if (!services || services->abi_version != luacs::kModuleAbiVersion ||
        !services->hud_print) {
        return luaL_error(state, "hud.dll received an incompatible LuaCS service table");
    }
    g_services = services;

    lua_createtable(state, 0, 17);
    add_function(state, "print", print_to);
    add_function(state, "broadcast", broadcast);
    add_function(state, "notify", notify);
    add_function(state, "console", console);
    add_function(state, "chat", chat);
    add_function(state, "center", center);
    add_function(state, "alert", alert);
    add_function(state, "notify_all", notify_all);
    add_function(state, "console_all", console_all);
    add_function(state, "chat_all", chat_all);
    add_function(state, "center_all", center_all);
    add_function(state, "alert_all", alert_all);

    add_integer(state, "NOTIFY", static_cast<int>(luacs::HudDestination::Notify));
    add_integer(state, "CONSOLE", static_cast<int>(luacs::HudDestination::Console));
    add_integer(state, "CHAT", static_cast<int>(luacs::HudDestination::Chat));
    add_integer(state, "CENTER", static_cast<int>(luacs::HudDestination::Center));
    add_integer(state, "ALERT", static_cast<int>(luacs::HudDestination::Alert));
    return 1;
}
