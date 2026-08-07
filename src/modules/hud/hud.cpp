#include "luacs/lua_checked.h"
#include "luacs/module_api.h"

extern "C" {
#include "lauxlib.h"
}

#include <string>
#include <string_view>

namespace {

const luacs::Services* g_services = nullptr;

int check_slot(lua_State* state, int index) {
    if (lua_isinteger(state, index)) {
        return luacs::lua_checked::checked_slot(state, index);
    }
    luaL_checktype(state, index, LUA_TTABLE);
    const int table = lua_absindex(state, index);
    lua_getfield(state, table, "slot");
    const int slot = luacs::lua_checked::checked_slot(state, -1);
    lua_pop(state, 1);
    return slot;
}

bool valid_destination(int destination) {
    return destination == static_cast<int>(luacs::HudDestination::Notify) ||
           destination == static_cast<int>(luacs::HudDestination::Console) ||
           destination == static_cast<int>(luacs::HudDestination::Chat) ||
           destination == static_cast<int>(luacs::HudDestination::Center) ||
           destination == static_cast<int>(luacs::HudDestination::Alert);
}

int check_destination(lua_State* state, int index) {
    const int destination = luacs::lua_checked::checked_int(
        state, index, std::numeric_limits<int>::min(),
        std::numeric_limits<int>::max(),
        "HUD destination must fit a 32-bit integer");
    if (!valid_destination(destination)) {
        return luaL_argerror(
            state, index,
            "HUD destination must be NOTIFY, CONSOLE, CHAT, CENTER, or ALERT");
    }
    return destination;
}

std::string check_message(lua_State* state, int index) {
    std::size_t size = 0;
    const char* raw = luaL_checklstring(state, index, &size);
    const std::string_view text(raw ? raw : "", size);
    if (text.find('\0') != std::string_view::npos) {
        luaL_argerror(state, index, "HUD message cannot contain NUL bytes");
    }
    return std::string(text);
}

int push_result(lua_State* state, bool result, const char* error) {
    if (result) {
        lua_pushboolean(state, 1);
        return 1;
    }
    lua_pushnil(state);
    lua_pushstring(state,
                   error && error[0] ? error : "CS2 rejected the HUD message");
    return 2;
}

int send(lua_State* state, int slot, int destination,
         const std::string& message) {
    char error[512]{};
    return push_result(
        state,
        g_services->hud_print(g_services->context, slot, destination,
                              message.c_str(), error, sizeof(error)),
        error);
}

int print_to(lua_State* state) {
    const int slot = check_slot(state, 1);
    const int destination = check_destination(state, 2);
    const std::string message = check_message(state, 3);
    return send(state, slot, destination, message);
}

int broadcast(lua_State* state) {
    const int destination = check_destination(state, 1);
    const std::string message = check_message(state, 2);
    return send(state, -1, destination, message);
}

#define LUACS_HUD_SINGLE(name, destination)                      \
    int name(lua_State* state) {                                 \
        const int slot = check_slot(state, 1);                   \
        const std::string message = check_message(state, 2);     \
        return send(state, slot, destination, message);          \
    }

#define LUACS_HUD_ALL(name, destination)                         \
    int name(lua_State* state) {                                 \
        const std::string message = check_message(state, 1);     \
        return send(state, -1, destination, message);            \
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
        return luaL_error(
            state, "hud.dll received an incompatible LuaCS service table");
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
    add_integer(state, "CONSOLE",
                static_cast<int>(luacs::HudDestination::Console));
    add_integer(state, "CHAT", static_cast<int>(luacs::HudDestination::Chat));
    add_integer(state, "CENTER", static_cast<int>(luacs::HudDestination::Center));
    add_integer(state, "ALERT", static_cast<int>(luacs::HudDestination::Alert));
    return 1;
}
