#include "luacs/module_api.h"

extern "C" {
#include "lauxlib.h"
}

#include <cctype>
#include <cstdint>
#include <string>

namespace {

using luacs::EventCallbackMode;
using luacs::PlayerInfo;
using luacs::Services;

inline constexpr const char* kEventMeta = "LuaCS.Event";
inline constexpr const char* kPlayerMeta = "LuaCS.Player";

const Services* services(lua_State* state) {
    return static_cast<const Services*>(
        lua_touserdata(state, lua_upvalueindex(1)));
}

std::uint64_t token(lua_State* state) {
    luaL_checktype(state, 1, LUA_TTABLE);
    lua_getfield(state, 1, "__token");
    const auto value =
        static_cast<std::uint64_t>(luaL_checkinteger(state, -1));
    lua_pop(state, 1);
    if (value == 0) {
        luaL_error(state,
                   "this is a lifecycle event, not a mutable CS2 game event");
    }
    return value;
}

void push_player(lua_State* state, const PlayerInfo& player) {
    lua_createtable(state, 0, 9);
    lua_pushinteger(state, player.slot);
    lua_setfield(state, -2, "slot");
    lua_pushstring(state, player.name ? player.name : "");
    lua_setfield(state, -2, "name");
    lua_pushinteger(state, static_cast<lua_Integer>(player.steam64));
    lua_setfield(state, -2, "steam64");
    lua_pushstring(state, player.steam_id ? player.steam_id : "");
    lua_setfield(state, -2, "steamid");
    lua_pushboolean(state, player.fake);
    lua_setfield(state, -2, "fake");
    lua_pushboolean(state, player.connected);
    lua_setfield(state, -2, "connected");
    lua_pushboolean(state, player.active);
    lua_setfield(state, -2, "active");

    luaL_getmetatable(state, kPlayerMeta);
    if (lua_istable(state, -1)) {
        lua_setmetatable(state, -2);
    } else {
        lua_pop(state, 1);
    }
}

int register_event(lua_State* state, bool post,
                   EventCallbackMode mode = EventCallbackMode::EventTable,
                   int name_index = 1, int callback_index = 2) {
    const auto* api = services(state);
    const char* name = luaL_checkstring(state, name_index);
    luaL_checktype(state, callback_index, LUA_TFUNCTION);
    if (!api || !api->event_on) {
        return luaL_error(state, "event service is unavailable");
    }
    const auto id = api->event_on(api->context, state, name, callback_index,
                                  mode, post);
    if (id == 0) return luaL_error(state, "could not register event callback");
    lua_pushinteger(state, static_cast<lua_Integer>(id));
    return 1;
}

int on(lua_State* state) { return register_event(state, false); }
int on_post(lua_State* state) { return register_event(state, true); }

int off(lua_State* state) {
    const auto* api = services(state);
    const auto id =
        static_cast<std::uint64_t>(luaL_checkinteger(state, 1));
    lua_pushboolean(state, api && api->event_off &&
                               api->event_off(api->context, state, id));
    return 1;
}

std::string snake_case(std::string value) {
    std::string result;
    result.reserve(value.size() + 4);
    for (std::size_t index = 0; index < value.size(); ++index) {
        const unsigned char current =
            static_cast<unsigned char>(value[index]);
        if (std::isupper(current)) {
            if (!result.empty() && result.back() != '_' &&
                (index + 1 == value.size() ||
                 std::islower(static_cast<unsigned char>(value[index + 1])))) {
                result.push_back('_');
            }
            result.push_back(static_cast<char>(std::tolower(current)));
        } else {
            result.push_back(static_cast<char>(current));
        }
    }
    return result;
}

int alias_register(lua_State* state) {
    const auto* api = static_cast<const Services*>(
        lua_touserdata(state, lua_upvalueindex(1)));
    const char* name = lua_tostring(state, lua_upvalueindex(2));
    const bool post = lua_toboolean(state, lua_upvalueindex(3)) != 0;
    const auto mode = static_cast<EventCallbackMode>(
        lua_tointeger(state, lua_upvalueindex(4)));

    const int callback_index = lua_isfunction(state, 1) ? 1 : 2;
    luaL_checktype(state, callback_index, LUA_TFUNCTION);
    if (!api || !api->event_on || !name) {
        return luaL_error(state, "event service is unavailable");
    }
    const auto id = api->event_on(api->context, state, name, callback_index,
                                  mode, post);
    if (id == 0) return luaL_error(state, "could not register event callback");
    lua_pushinteger(state, static_cast<lua_Integer>(id));
    return 1;
}

int instance_index(lua_State* state) {
    const auto* api = services(state);
    const char* key = luaL_checkstring(state, 2);
    std::string name(key);
    bool post = false;
    EventCallbackMode mode = EventCallbackMode::EventTable;

    if (name.rfind("OnPost", 0) == 0 && name.size() > 6) {
        post = true;
        name.erase(0, 6);
    } else if (name.rfind("On", 0) == 0 && name.size() > 2) {
        name.erase(0, 2);
    } else {
        lua_pushnil(state);
        return 1;
    }

    name = snake_case(name);
    if (!post && (name == "player_activate" || name == "player_connect" ||
                  name == "player_put_in_server" ||
                  name == "player_disconnect")) {
        mode = EventCallbackMode::PlayerOnly;
    }

    lua_pushlightuserdata(state, const_cast<Services*>(api));
    lua_pushlstring(state, name.data(), name.size());
    lua_pushboolean(state, post);
    lua_pushinteger(state, static_cast<lua_Integer>(mode));
    lua_pushcclosure(state, &alias_register, 4);
    return 1;
}

int has_key(lua_State* state) {
    const auto* api = services(state);
    const char* key = luaL_checkstring(state, 2);
    lua_pushboolean(state, api && api->event_has_key &&
                               api->event_has_key(api->context, token(state),
                                                  key));
    return 1;
}

int is_empty(lua_State* state) {
    const auto* api = services(state);
    const char* key = luaL_checkstring(state, 2);
    lua_pushboolean(state, api && api->event_is_empty &&
                               api->event_is_empty(api->context, token(state),
                                                   key));
    return 1;
}

int get_bool(lua_State* state) {
    const auto* api = services(state);
    const char* key = luaL_checkstring(state, 2);
    const bool fallback = lua_toboolean(state, 3) != 0;
    bool value = fallback;
    if (!api || !api->event_get_bool ||
        !api->event_get_bool(api->context, token(state), key, fallback,
                             &value)) {
        lua_pushnil(state);
        return 1;
    }
    lua_pushboolean(state, value);
    return 1;
}

int get_int(lua_State* state) {
    const auto* api = services(state);
    const char* key = luaL_checkstring(state, 2);
    const int fallback =
        static_cast<int>(luaL_optinteger(state, 3, 0));
    int value = fallback;
    if (!api || !api->event_get_int ||
        !api->event_get_int(api->context, token(state), key, fallback,
                            &value)) {
        lua_pushnil(state);
        return 1;
    }
    lua_pushinteger(state, value);
    return 1;
}

int get_uint64(lua_State* state) {
    const auto* api = services(state);
    const char* key = luaL_checkstring(state, 2);
    const auto fallback =
        static_cast<std::uint64_t>(luaL_optinteger(state, 3, 0));
    std::uint64_t value = fallback;
    if (!api || !api->event_get_uint64 ||
        !api->event_get_uint64(api->context, token(state), key, fallback,
                               &value)) {
        lua_pushnil(state);
        return 1;
    }
    lua_pushinteger(state, static_cast<lua_Integer>(value));
    return 1;
}

int get_float(lua_State* state) {
    const auto* api = services(state);
    const char* key = luaL_checkstring(state, 2);
    const float fallback =
        static_cast<float>(luaL_optnumber(state, 3, 0.0));
    float value = fallback;
    if (!api || !api->event_get_float ||
        !api->event_get_float(api->context, token(state), key, fallback,
                              &value)) {
        lua_pushnil(state);
        return 1;
    }
    lua_pushnumber(state, value);
    return 1;
}

int get_string(lua_State* state) {
    const auto* api = services(state);
    const char* key = luaL_checkstring(state, 2);
    const char* fallback = luaL_optstring(state, 3, "");
    char value[2048]{};
    if (!api || !api->event_get_string ||
        !api->event_get_string(api->context, token(state), key, fallback,
                               value, sizeof(value))) {
        lua_pushnil(state);
        return 1;
    }
    lua_pushstring(state, value);
    return 1;
}

int get_player_slot(lua_State* state) {
    const auto* api = services(state);
    const char* key = luaL_checkstring(state, 2);
    int value = -1;
    if (!api || !api->event_get_player_slot ||
        !api->event_get_player_slot(api->context, token(state), key,
                                    &value)) {
        lua_pushnil(state);
        return 1;
    }
    lua_pushinteger(state, value);
    return 1;
}

int get_entity_index(lua_State* state) {
    const auto* api = services(state);
    const char* key = luaL_checkstring(state, 2);
    int value = -1;
    if (!api || !api->event_get_entity_index ||
        !api->event_get_entity_index(api->context, token(state), key,
                                     &value)) {
        lua_pushnil(state);
        return 1;
    }
    lua_pushinteger(state, value);
    return 1;
}

int get_pawn_index(lua_State* state) {
    const auto* api = services(state);
    const char* key = luaL_checkstring(state, 2);
    int value = -1;
    if (!api || !api->event_get_pawn_index ||
        !api->event_get_pawn_index(api->context, token(state), key,
                                   &value)) {
        lua_pushnil(state);
        return 1;
    }
    lua_pushinteger(state, value);
    return 1;
}

int get_player(lua_State* state) {
    const auto* api = services(state);
    const char* key = luaL_checkstring(state, 2);
    int slot = -1;
    if (!api || !api->event_get_player_slot || !api->player_get ||
        !api->event_get_player_slot(api->context, token(state), key,
                                    &slot)) {
        lua_pushnil(state);
        return 1;
    }
    PlayerInfo player;
    if (!api->player_get(api->context, slot, &player)) {
        lua_pushnil(state);
        return 1;
    }
    push_player(state, player);
    return 1;
}

int set_bool(lua_State* state) {
    const auto* api = services(state);
    const char* key = luaL_checkstring(state, 2);
    const bool value = lua_toboolean(state, 3) != 0;
    lua_pushboolean(state, api && api->event_set_bool &&
                               api->event_set_bool(api->context, token(state),
                                                   key, value));
    return 1;
}

int set_int(lua_State* state) {
    const auto* api = services(state);
    const char* key = luaL_checkstring(state, 2);
    const int value = static_cast<int>(luaL_checkinteger(state, 3));
    lua_pushboolean(state, api && api->event_set_int &&
                               api->event_set_int(api->context, token(state),
                                                  key, value));
    return 1;
}

int set_uint64(lua_State* state) {
    const auto* api = services(state);
    const char* key = luaL_checkstring(state, 2);
    const auto value =
        static_cast<std::uint64_t>(luaL_checkinteger(state, 3));
    lua_pushboolean(state, api && api->event_set_uint64 &&
                               api->event_set_uint64(api->context, token(state),
                                                     key, value));
    return 1;
}

int set_float(lua_State* state) {
    const auto* api = services(state);
    const char* key = luaL_checkstring(state, 2);
    const float value = static_cast<float>(luaL_checknumber(state, 3));
    lua_pushboolean(state, api && api->event_set_float &&
                               api->event_set_float(api->context, token(state),
                                                    key, value));
    return 1;
}

int set_string(lua_State* state) {
    const auto* api = services(state);
    const char* key = luaL_checkstring(state, 2);
    const char* value = luaL_checkstring(state, 3);
    lua_pushboolean(state, api && api->event_set_string &&
                               api->event_set_string(api->context, token(state),
                                                     key, value));
    return 1;
}

int cancel(lua_State* state) {
    const auto* api = services(state);
    lua_pushboolean(state, api && api->event_cancel &&
                               api->event_cancel(api->context, token(state)));
    return 1;
}

int set_dont_broadcast(lua_State* state) {
    const auto* api = services(state);
    const bool value = lua_toboolean(state, 2) != 0;
    const bool result =
        api && api->event_set_dont_broadcast &&
        api->event_set_dont_broadcast(api->context, token(state), value);
    if (result) {
        lua_pushboolean(state, value);
        lua_setfield(state, 1, "dont_broadcast");
    }
    lua_pushboolean(state, result);
    return 1;
}

int event_tostring(lua_State* state) {
    lua_getfield(state, 1, "name");
    const char* name = lua_tostring(state, -1);
    lua_getfield(state, 1, "post");
    const bool post = lua_toboolean(state, -1) != 0;
    lua_pushfstring(state, "GameEvent(%s, %s)", name ? name : "?",
                    post ? "post" : "pre");
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
        return luaL_error(state, "LuaCS events ABI mismatch");
    }

    if (luaL_newmetatable(state, kEventMeta)) {
        lua_newtable(state);
        add_function(state, api, "has_key", &has_key);
        add_function(state, api, "is_empty", &is_empty);
        add_function(state, api, "get_bool", &get_bool);
        add_function(state, api, "get_int", &get_int);
        add_function(state, api, "get_uint64", &get_uint64);
        add_function(state, api, "get_float", &get_float);
        add_function(state, api, "get_string", &get_string);
        add_function(state, api, "get_player_slot", &get_player_slot);
        add_function(state, api, "get_entity_index", &get_entity_index);
        add_function(state, api, "get_pawn_index", &get_pawn_index);
        add_function(state, api, "get_player", &get_player);
        add_function(state, api, "set_bool", &set_bool);
        add_function(state, api, "set_int", &set_int);
        add_function(state, api, "set_uint64", &set_uint64);
        add_function(state, api, "set_float", &set_float);
        add_function(state, api, "set_string", &set_string);
        add_function(state, api, "cancel", &cancel);
        add_function(state, api, "set_dont_broadcast",
                     &set_dont_broadcast);
        lua_setfield(state, -2, "__index");

        add_function(state, api, "__tostring", &event_tostring);
    }
    lua_pop(state, 1);

    lua_createtable(state, 0, 5);
    add_function(state, api, "on", &on);
    add_function(state, api, "on_post", &on_post);
    add_function(state, api, "off", &off);

    lua_newtable(state);
    lua_newtable(state);
    add_function(state, api, "__index", &instance_index);
    lua_setmetatable(state, -2);
    lua_setfield(state, -2, "Instance");

    return 1;
}
