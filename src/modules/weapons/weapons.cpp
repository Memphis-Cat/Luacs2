#include "luacs/module_api.h"

extern "C" {
#include "lauxlib.h"
}

#include <algorithm>
#include <cctype>
#include <cstring>
#include <string>
#include <string_view>

namespace {

using luacs::Services;
using luacs::WeaponInfo;

inline constexpr const char* kWeaponMeta = "LuaCS.Weapon";

const Services* services(lua_State* state) {
    return static_cast<const Services*>(
        lua_touserdata(state, lua_upvalueindex(1)));
}

int check_slot(lua_State* state, int index) {
    if (lua_isinteger(state, index)) {
        return static_cast<int>(lua_tointeger(state, index));
    }
    luaL_checktype(state, index, LUA_TTABLE);
    lua_getfield(state, index, "slot");
    if (!lua_isinteger(state, -1)) {
        lua_pop(state, 1);
        return luaL_argerror(
            state, index,
            "expected a player table containing an integer slot");
    }
    const int slot = static_cast<int>(lua_tointeger(state, -1));
    lua_pop(state, 1);
    return slot;
}

int check_entity_index(lua_State* state, int index) {
    if (lua_isinteger(state, index)) {
        return static_cast<int>(lua_tointeger(state, index));
    }
    luaL_checktype(state, index, LUA_TTABLE);
    lua_getfield(state, index, "entity_index");
    if (!lua_isinteger(state, -1)) {
        lua_pop(state, 1);
        return luaL_argerror(
            state, index,
            "expected a weapon table containing an integer entity_index");
    }
    const int entity_index = static_cast<int>(lua_tointeger(state, -1));
    lua_pop(state, 1);
    return entity_index;
}

int owner_slot(lua_State* state, int index) {
    luaL_checktype(state, index, LUA_TTABLE);
    lua_getfield(state, index, "owner_slot");
    const int slot = static_cast<int>(luaL_checkinteger(state, -1));
    lua_pop(state, 1);
    return slot;
}

int fail(lua_State* state, const char* error) {
    lua_pushnil(state);
    lua_pushstring(state,
                   error && *error ? error : "CS2 rejected the weapon operation");
    return 2;
}

int success(lua_State* state, bool result, const char* error) {
    if (!result) return fail(state, error);
    lua_pushboolean(state, true);
    return 1;
}

bool valid_class_name(std::string_view name) {
    if (name.empty() || name.size() >= luacs::kClassnameCapacity) return false;
    for (const unsigned char character : name) {
        if (std::isspace(character) || std::iscntrl(character)) return false;
    }
    return true;
}

void set_integer(lua_State* state, int table, const char* field,
                 lua_Integer value) {
    table = lua_absindex(state, table);
    lua_pushinteger(state, value);
    lua_setfield(state, table, field);
}

void set_boolean(lua_State* state, int table, const char* field, bool value) {
    table = lua_absindex(state, table);
    lua_pushboolean(state, value);
    lua_setfield(state, table, field);
}

void apply_weapon(lua_State* state, int table, const WeaponInfo& weapon) {
    table = lua_absindex(state, table);
    set_boolean(state, table, "valid", weapon.valid);
    set_integer(state, table, "entity_index", weapon.entity_index);
    set_integer(state, table, "handle", weapon.handle);
    set_integer(state, table, "owner_slot", weapon.owner_slot);
    set_boolean(state, table, "active", weapon.active);
    set_integer(state, table, "clip1", weapon.clip1);
    set_integer(state, table, "clip2", weapon.clip2);
    set_integer(state, table, "reserve1", weapon.reserve1);
    set_integer(state, table, "reserve2", weapon.reserve2);
    lua_pushstring(state, weapon.classname);
    lua_setfield(state, table, "classname");
}

void push_weapon(lua_State* state, const WeaponInfo& weapon) {
    lua_createtable(state, 0, 11);
    apply_weapon(state, -1, weapon);
    luaL_getmetatable(state, kWeaponMeta);
    lua_setmetatable(state, -2);
}

bool get_weapon(const Services* api, int entity_index, WeaponInfo& output,
                char* error, std::size_t error_size) {
    return api && api->weapon_get &&
           api->weapon_get(api->context, entity_index, &output, error,
                           error_size);
}

int give(lua_State* state) {
    const auto* api = services(state);
    const int slot = check_slot(state, 1);
    const char* class_name = luaL_checkstring(state, 2);
    if (!valid_class_name(class_name)) {
        return luaL_argerror(
            state, 2,
            "expected an exact CS2 classname such as weapon_ak47");
    }

    WeaponInfo weapon;
    char error[512]{};
    if (!api || !api->weapon_give ||
        !api->weapon_give(api->context, slot, class_name, &weapon, error,
                          sizeof(error))) {
        return fail(state, error);
    }
    push_weapon(state, weapon);
    return 1;
}

int list(lua_State* state) {
    const auto* api = services(state);
    const int slot = check_slot(state, 1);
    char error[512]{};
    if (!api || !api->weapon_count || !api->weapon_at) {
        return fail(state, "weapon inventory service is unavailable");
    }
    const std::size_t count =
        api->weapon_count(api->context, slot, error, sizeof(error));
    if (error[0]) return fail(state, error);

    lua_createtable(state, static_cast<int>(count), 0);
    for (std::size_t index = 0; index < count; ++index) {
        WeaponInfo weapon;
        error[0] = '\0';
        if (!api->weapon_at(api->context, slot, index, &weapon, error,
                            sizeof(error))) {
            return fail(state, error);
        }
        push_weapon(state, weapon);
        lua_seti(state, -2, static_cast<lua_Integer>(index + 1));
    }
    return 1;
}

int count(lua_State* state) {
    const auto* api = services(state);
    const int slot = check_slot(state, 1);
    char error[512]{};
    if (!api || !api->weapon_count) {
        return fail(state, "weapon inventory service is unavailable");
    }
    const std::size_t result =
        api->weapon_count(api->context, slot, error, sizeof(error));
    if (error[0]) return fail(state, error);
    lua_pushinteger(state, static_cast<lua_Integer>(result));
    return 1;
}

int get(lua_State* state) {
    const auto* api = services(state);
    const int entity_index = check_entity_index(state, 1);
    WeaponInfo weapon;
    char error[512]{};
    if (!get_weapon(api, entity_index, weapon, error, sizeof(error))) {
        return fail(state, error);
    }
    push_weapon(state, weapon);
    return 1;
}

int refresh(lua_State* state) {
    const auto* api = services(state);
    const int entity_index = check_entity_index(state, 1);
    WeaponInfo weapon;
    char error[512]{};
    if (!get_weapon(api, entity_index, weapon, error, sizeof(error))) {
        return fail(state, error);
    }
    if (lua_istable(state, 1)) {
        apply_weapon(state, 1, weapon);
        lua_pushvalue(state, 1);
    } else {
        push_weapon(state, weapon);
    }
    return 1;
}

int active(lua_State* state) {
    const auto* api = services(state);
    const int slot = check_slot(state, 1);
    char error[512]{};
    if (!api || !api->weapon_count || !api->weapon_at) {
        return fail(state, "weapon inventory service is unavailable");
    }
    const std::size_t total =
        api->weapon_count(api->context, slot, error, sizeof(error));
    if (error[0]) return fail(state, error);
    for (std::size_t index = 0; index < total; ++index) {
        WeaponInfo weapon;
        if (!api->weapon_at(api->context, slot, index, &weapon, error,
                            sizeof(error))) {
            return fail(state, error);
        }
        if (weapon.active) {
            push_weapon(state, weapon);
            return 1;
        }
    }
    lua_pushnil(state);
    return 1;
}

std::string lower(std::string_view value) {
    std::string result(value);
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char c) {
                       return static_cast<char>(std::tolower(c));
                   });
    return result;
}

int find(lua_State* state) {
    const auto* api = services(state);
    const int slot = check_slot(state, 1);
    const std::string needle = lower(luaL_checkstring(state, 2));
    char error[512]{};
    if (!api || !api->weapon_count || !api->weapon_at) {
        return fail(state, "weapon inventory service is unavailable");
    }
    const std::size_t total =
        api->weapon_count(api->context, slot, error, sizeof(error));
    if (error[0]) return fail(state, error);
    for (std::size_t index = 0; index < total; ++index) {
        WeaponInfo weapon;
        if (!api->weapon_at(api->context, slot, index, &weapon, error,
                            sizeof(error))) {
            return fail(state, error);
        }
        if (lower(weapon.classname) == needle) {
            push_weapon(state, weapon);
            return 1;
        }
    }
    lua_pushnil(state);
    return 1;
}

int has(lua_State* state) {
    const int results = find(state);
    if (results != 1) return results;
    const bool present = !lua_isnil(state, -1);
    lua_pop(state, 1);
    lua_pushboolean(state, present);
    return 1;
}

int remove_all(lua_State* state) {
    const auto* api = services(state);
    const int slot = check_slot(state, 1);
    char error[512]{};
    return success(state,
                   api && api->weapon_remove_all &&
                       api->weapon_remove_all(api->context, slot, error,
                                              sizeof(error)),
                   error);
}

int drop_active(lua_State* state) {
    const auto* api = services(state);
    const int slot = check_slot(state, 1);
    char error[512]{};
    return success(state,
                   api && api->weapon_drop_active &&
                       api->weapon_drop_active(api->context, slot, error,
                                               sizeof(error)),
                   error);
}

int remove_weapon(lua_State* state) {
    const auto* api = services(state);
    const int slot = check_slot(state, 1);
    const int entity_index = check_entity_index(state, 2);
    const bool delete_entity = lua_isnoneornil(state, 3)
                                   ? true
                                   : lua_toboolean(state, 3) != 0;
    char error[512]{};
    return success(state,
                   api && api->weapon_remove &&
                       api->weapon_remove(api->context, slot, entity_index,
                                          delete_entity, error, sizeof(error)),
                   error);
}

int remove_by_classname(lua_State* state) {
    const auto* api = services(state);
    const int slot = check_slot(state, 1);
    const std::string needle = lower(luaL_checkstring(state, 2));
    const bool delete_entity = lua_isnoneornil(state, 3)
                                   ? true
                                   : lua_toboolean(state, 3) != 0;
    char error[512]{};
    if (!api || !api->weapon_count || !api->weapon_at ||
        !api->weapon_remove) {
        return fail(state, "weapon removal service is unavailable");
    }

    const std::size_t total =
        api->weapon_count(api->context, slot, error, sizeof(error));
    if (error[0]) return fail(state, error);
    int removed = 0;
    for (std::size_t reverse = total; reverse > 0; --reverse) {
        WeaponInfo weapon;
        if (!api->weapon_at(api->context, slot, reverse - 1, &weapon, error,
                            sizeof(error))) {
            return fail(state, error);
        }
        if (lower(weapon.classname) != needle) continue;
        if (!api->weapon_remove(api->context, slot, weapon.entity_index,
                                delete_entity, error, sizeof(error))) {
            return fail(state, error);
        }
        ++removed;
    }
    lua_pushinteger(state, removed);
    return 1;
}

int drop(lua_State* state) {
    const auto* api = services(state);
    const int slot = check_slot(state, 1);
    const int entity_index = check_entity_index(state, 2);
    char error[512]{};
    return success(state,
                   api && api->weapon_drop &&
                       api->weapon_drop(api->context, slot, entity_index,
                                        error, sizeof(error)),
                   error);
}

int switch_to(lua_State* state) {
    const auto* api = services(state);
    const int slot = check_slot(state, 1);
    const int entity_index = check_entity_index(state, 2);
    char error[512]{};
    return success(state,
                   api && api->weapon_switch &&
                       api->weapon_switch(api->context, slot, entity_index,
                                          error, sizeof(error)),
                   error);
}

int set_clip(lua_State* state, int clip_index, const char* field) {
    const auto* api = services(state);
    const int entity_index = check_entity_index(state, 1);
    const int value = static_cast<int>(luaL_checkinteger(state, 2));
    if (value < -1) return luaL_argerror(state, 2, "clip must be -1 or greater");
    char error[512]{};
    if (!api || !api->weapon_set_clip ||
        !api->weapon_set_clip(api->context, entity_index, clip_index, value,
                              error, sizeof(error))) {
        return fail(state, error);
    }
    if (lua_istable(state, 1)) set_integer(state, 1, field, value);
    lua_pushboolean(state, true);
    return 1;
}

int set_clip1(lua_State* state) { return set_clip(state, 0, "clip1"); }
int set_clip2(lua_State* state) { return set_clip(state, 1, "clip2"); }

int set_reserve(lua_State* state, int reserve_index, const char* field) {
    const auto* api = services(state);
    const int entity_index = check_entity_index(state, 1);
    const int value = static_cast<int>(luaL_checkinteger(state, 2));
    if (value < 0) return luaL_argerror(state, 2, "reserve ammo cannot be negative");
    char error[512]{};
    if (!api || !api->weapon_set_reserve ||
        !api->weapon_set_reserve(api->context, entity_index, reserve_index,
                                 value, error, sizeof(error))) {
        return fail(state, error);
    }
    if (lua_istable(state, 1)) set_integer(state, 1, field, value);
    lua_pushboolean(state, true);
    return 1;
}

int set_reserve1(lua_State* state) {
    return set_reserve(state, 0, "reserve1");
}

int set_reserve2(lua_State* state) {
    return set_reserve(state, 1, "reserve2");
}

int get_ammo(lua_State* state) {
    const auto* api = services(state);
    const int slot = check_slot(state, 1);
    const int ammo_type = static_cast<int>(luaL_checkinteger(state, 2));
    if (ammo_type < 0 || ammo_type >= 32) {
        return luaL_argerror(state, 2, "ammo type must be between 0 and 31");
    }
    int value = 0;
    char error[512]{};
    if (!api || !api->weapon_get_ammo ||
        !api->weapon_get_ammo(api->context, slot, ammo_type, &value, error,
                              sizeof(error))) {
        return fail(state, error);
    }
    lua_pushinteger(state, value);
    return 1;
}

int set_ammo(lua_State* state) {
    const auto* api = services(state);
    const int slot = check_slot(state, 1);
    const int ammo_type = static_cast<int>(luaL_checkinteger(state, 2));
    const int value = static_cast<int>(luaL_checkinteger(state, 3));
    if (ammo_type < 0 || ammo_type >= 32) {
        return luaL_argerror(state, 2, "ammo type must be between 0 and 31");
    }
    if (value < 0 || value > 65535) {
        return luaL_argerror(state, 3, "ammo must be between 0 and 65535");
    }
    char error[512]{};
    return success(state,
                   api && api->weapon_set_ammo &&
                       api->weapon_set_ammo(api->context, slot, ammo_type,
                                            value, error, sizeof(error)),
                   error);
}

int method_remove(lua_State* state) {
    const auto* api = services(state);
    const int slot = owner_slot(state, 1);
    const int entity_index = check_entity_index(state, 1);
    const bool delete_entity = lua_isnoneornil(state, 2)
                                   ? true
                                   : lua_toboolean(state, 2) != 0;
    char error[512]{};
    if (!api || !api->weapon_remove ||
        !api->weapon_remove(api->context, slot, entity_index, delete_entity,
                            error, sizeof(error))) {
        return fail(state, error);
    }
    set_boolean(state, 1, "valid", false);
    lua_pushboolean(state, true);
    return 1;
}

int method_drop(lua_State* state) {
    const auto* api = services(state);
    const int slot = owner_slot(state, 1);
    const int entity_index = check_entity_index(state, 1);
    char error[512]{};
    if (!api || !api->weapon_drop ||
        !api->weapon_drop(api->context, slot, entity_index, error,
                          sizeof(error))) {
        return fail(state, error);
    }
    set_integer(state, 1, "owner_slot", -1);
    set_boolean(state, 1, "active", false);
    lua_pushboolean(state, true);
    return 1;
}

int method_switch(lua_State* state) {
    const auto* api = services(state);
    const int slot = owner_slot(state, 1);
    const int entity_index = check_entity_index(state, 1);
    char error[512]{};
    if (!api || !api->weapon_switch ||
        !api->weapon_switch(api->context, slot, entity_index, error,
                            sizeof(error))) {
        return fail(state, error);
    }
    set_boolean(state, 1, "active", true);
    lua_pushboolean(state, true);
    return 1;
}

int weapon_tostring(lua_State* state) {
    lua_getfield(state, 1, "classname");
    const char* classname = lua_tostring(state, -1);
    lua_getfield(state, 1, "entity_index");
    const int index = static_cast<int>(lua_tointeger(state, -1));
    lua_pushfstring(state, "Weapon(%d, %s)", index,
                    classname ? classname : "");
    return 1;
}

void add_function(lua_State* state, const Services* api, const char* name,
                  lua_CFunction function) {
    lua_pushlightuserdata(state, const_cast<Services*>(api));
    lua_pushcclosure(state, function, 1);
    lua_setfield(state, -2, name);
}

} // namespace

LUACS_MODULE_EXPORT int LuaCS_OpenModule(lua_State* state,
                                         const Services* api) {
    if (!api || api->abi_version != luacs::kModuleAbiVersion) {
        return luaL_error(state, "LuaCS weapons ABI mismatch");
    }

    if (luaL_newmetatable(state, kWeaponMeta)) {
        lua_newtable(state);
        add_function(state, api, "refresh", &refresh);
        add_function(state, api, "set_clip1", &set_clip1);
        add_function(state, api, "set_clip2", &set_clip2);
        add_function(state, api, "set_reserve1", &set_reserve1);
        add_function(state, api, "set_reserve2", &set_reserve2);
        add_function(state, api, "remove", &method_remove);
        add_function(state, api, "drop", &method_drop);
        add_function(state, api, "switch", &method_switch);
        lua_setfield(state, -2, "__index");
        add_function(state, api, "__tostring", &weapon_tostring);
    }
    lua_pop(state, 1);

    lua_createtable(state, 0, 20);
    add_function(state, api, "give", &give);
    add_function(state, api, "list", &list);
    add_function(state, api, "count", &count);
    add_function(state, api, "get", &get);
    add_function(state, api, "active", &active);
    add_function(state, api, "find", &find);
    add_function(state, api, "has", &has);
    add_function(state, api, "refresh", &refresh);
    add_function(state, api, "remove", &remove_weapon);
    add_function(state, api, "remove_by_classname", &remove_by_classname);
    add_function(state, api, "remove_all", &remove_all);
    add_function(state, api, "drop", &drop);
    add_function(state, api, "drop_active", &drop_active);
    add_function(state, api, "switch", &switch_to);
    add_function(state, api, "set_clip1", &set_clip1);
    add_function(state, api, "set_clip2", &set_clip2);
    add_function(state, api, "set_reserve1", &set_reserve1);
    add_function(state, api, "set_reserve2", &set_reserve2);
    add_function(state, api, "get_ammo", &get_ammo);
    add_function(state, api, "set_ammo", &set_ammo);
    return 1;
}
