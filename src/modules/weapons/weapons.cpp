#include "luacs/module_api.h"

extern "C" {
#include "lauxlib.h"
}

#include <cctype>
#include <string_view>

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
            return luaL_argerror(state, index, "expected a player table containing an integer slot");
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
    lua_pushstring(state, error && error[0] ? error : "CS2 rejected the weapon operation");
    return 2;
}

bool valid_class_name(std::string_view name) {
    if (name.empty() || name.size() > 127) return false;
    for (const unsigned char character : name) {
        if (std::isspace(character) || std::iscntrl(character)) return false;
    }
    return true;
}

int give(lua_State* state) {
    const int slot = check_slot(state, 1);
    const char* class_name = luaL_checkstring(state, 2);
    if (!valid_class_name(class_name)) {
        return luaL_argerror(state, 2,
                             "expected an exact CS2 item classname such as weapon_ak47");
    }
    char error[512]{};
    return push_result(state,
                       g_services->weapon_give(g_services->context, slot, class_name,
                                               error, sizeof(error)),
                       error);
}

int remove_all(lua_State* state) {
    const int slot = check_slot(state, 1);
    char error[512]{};
    return push_result(state,
                       g_services->weapon_remove_all(g_services->context, slot, error,
                                                     sizeof(error)),
                       error);
}

int drop_active(lua_State* state) {
    const int slot = check_slot(state, 1);
    char error[512]{};
    return push_result(state,
                       g_services->weapon_drop_active(g_services->context, slot, error,
                                                      sizeof(error)),
                       error);
}

} // namespace

LUACS_MODULE_EXPORT int LuaCS_OpenModule(lua_State* state,
                                         const luacs::Services* services) {
    if (!services || services->abi_version != luacs::kModuleAbiVersion ||
        !services->weapon_give || !services->weapon_remove_all ||
        !services->weapon_drop_active) {
        return luaL_error(state,
                          "weapons.dll received an incompatible LuaCS service table");
    }
    g_services = services;

    lua_createtable(state, 0, 3);
    lua_pushcfunction(state, give);
    lua_setfield(state, -2, "give");
    lua_pushcfunction(state, remove_all);
    lua_setfield(state, -2, "remove_all");
    lua_pushcfunction(state, drop_active);
    lua_setfield(state, -2, "drop_active");
    return 1;
}
