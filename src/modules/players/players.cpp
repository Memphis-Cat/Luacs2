#include "luacs/lua_checked.h"
#include "luacs/module_api.h"

extern "C" {
#include "lauxlib.h"
}

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>

namespace {

using luacs::PlayerBoolField;
using luacs::PlayerInfo;
using luacs::PlayerIntField;
using luacs::PlayerState;
using luacs::Services;
using luacs::Vector3;

inline constexpr const char* kPlayerMeta = "LuaCS.Player";

const Services* services(lua_State* state) {
    return static_cast<const Services*>(
        lua_touserdata(state, lua_upvalueindex(1)));
}

int slot_from(lua_State* state, int index) {
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

bool finite_vector(const Vector3& value) {
    return std::isfinite(value.x) && std::isfinite(value.y) &&
           std::isfinite(value.z);
}

void push_vector(lua_State* state, const Vector3& value) {
    lua_createtable(state, 0, 4);
    lua_pushnumber(state, value.x);
    lua_setfield(state, -2, "x");
    lua_pushnumber(state, value.y);
    lua_setfield(state, -2, "y");
    lua_pushnumber(state, value.z);
    lua_setfield(state, -2, "z");
    lua_pushliteral(state, "Vector");
    lua_setfield(state, -2, "__type");
}

void set_int(lua_State* state, int table, const char* key, lua_Integer value) {
    table = lua_absindex(state, table);
    lua_pushinteger(state, value);
    lua_setfield(state, table, key);
}

void set_bool(lua_State* state, int table, const char* key, bool value) {
    table = lua_absindex(state, table);
    lua_pushboolean(state, value);
    lua_setfield(state, table, key);
}

bool valid_player_state(const PlayerState& value) {
    if (!value.valid) return false;
    if (value.controller_index < -1 || value.pawn_index < -1) return false;
    return finite_vector(value.position) && finite_vector(value.velocity) &&
           finite_vector(value.eye_angles);
}

void apply_state(lua_State* state, int table, const PlayerState& value) {
    table = lua_absindex(state, table);
    set_bool(state, table, "valid", value.valid);
    set_bool(state, table, "has_controller", value.has_controller);
    set_bool(state, table, "has_pawn", value.has_pawn);
    set_bool(state, table, "alive", value.alive);
    set_int(state, table, "controller_index", value.controller_index);
    set_int(state, table, "pawn_index", value.pawn_index);
    set_int(state, table, "pawn_handle", value.pawn_handle);
    set_int(state, table, "health", value.health);
    set_int(state, table, "max_health", value.max_health);
    set_int(state, table, "armor", value.armor);
    set_int(state, table, "team", value.team);
    set_int(state, table, "money", value.money);
    set_int(state, table, "ping", value.ping);
    set_bool(state, table, "helmet", value.helmet);
    set_bool(state, table, "defuser", value.defuser);
    set_bool(state, table, "on_ground", value.on_ground);

    push_vector(state, value.position);
    lua_setfield(state, table, "position");
    push_vector(state, value.velocity);
    lua_setfield(state, table, "velocity");
    push_vector(state, value.eye_angles);
    lua_setfield(state, table, "eye_angles");
}

void push_player(lua_State* state, const Services* api, const PlayerInfo& player,
                 bool refresh_live = true) {
    lua_createtable(state, 0, 28);
    set_int(state, -1, "slot", player.slot);
    lua_pushstring(state, player.name ? player.name : "");
    lua_setfield(state, -2, "name");
    luacs::lua_checked::push_u64_exact(state, player.steam64);
    lua_setfield(state, -2, "steam64");
    lua_pushstring(state, player.steam_id ? player.steam_id : "");
    lua_setfield(state, -2, "steamid");
    set_bool(state, -1, "fake", player.fake);
    set_bool(state, -1, "connected", player.connected);
    set_bool(state, -1, "active", player.active);

    if (refresh_live && api->player_state && player.slot >= 0 &&
        player.slot < 64) {
        PlayerState live;
        char error[256]{};
        if (api->player_state(api->context, player.slot, &live, error,
                              sizeof(error)) &&
            valid_player_state(live)) {
            apply_state(state, -1, live);
        } else {
            set_bool(state, -1, "valid", false);
            set_bool(state, -1, "has_controller", false);
            set_bool(state, -1, "has_pawn", false);
            set_bool(state, -1, "alive", false);
        }
    }

    luaL_getmetatable(state, kPlayerMeta);
    lua_setmetatable(state, -2);
}

int fail(lua_State* state, const char* message) {
    lua_pushnil(state);
    lua_pushstring(state, message && *message ? message : "operation failed");
    return 2;
}

int refresh(lua_State* state) {
    const auto* api = services(state);
    const int slot = slot_from(state, 1);
    if (!api || !api->player_state) {
        return fail(state, "player state service is unavailable");
    }

    PlayerState value;
    char error[256]{};
    if (!api->player_state(api->context, slot, &value, error,
                           sizeof(error))) {
        return fail(state, error);
    }
    if (!valid_player_state(value)) {
        return fail(state, "Source 2 returned invalid player state");
    }

    if (lua_istable(state, 1)) {
        apply_state(state, 1, value);
        lua_pushvalue(state, 1);
    } else {
        PlayerInfo player;
        if (!api->player_get || !api->player_get(api->context, slot, &player)) {
            return fail(state, "player metadata is unavailable");
        }
        push_player(state, api, player, false);
        apply_state(state, -1, value);
    }
    return 1;
}

int get_by_slot(lua_State* state) {
    const auto* api = services(state);
    const int slot = luacs::lua_checked::checked_slot(state, 1);
    PlayerInfo player;
    if (!api || !api->player_get ||
        !api->player_get(api->context, slot, &player)) {
        lua_pushnil(state);
        return 1;
    }
    push_player(state, api, player);
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

int get(lua_State* state) {
    const auto* api = services(state);
    if (!api || !api->player_count || !api->player_at) {
        lua_pushnil(state);
        return 1;
    }

    if (lua_isinteger(state, 1)) {
        const lua_Integer needle = lua_tointeger(state, 1);
        PlayerInfo by_slot;
        if (needle >= 0 && needle < 64 && api->player_get &&
            api->player_get(api->context, static_cast<int>(needle),
                            &by_slot)) {
            push_player(state, api, by_slot);
            return 1;
        }

        if (needle >= 0) {
            const auto steam_needle = static_cast<std::uint64_t>(needle);
            const std::size_t count = api->player_count(api->context);
            for (std::size_t index = 0; index < count; ++index) {
                PlayerInfo player;
                if (api->player_at(api->context, index, &player) &&
                    player.steam64 == steam_needle) {
                    push_player(state, api, player);
                    return 1;
                }
            }
        }
        lua_pushnil(state);
        return 1;
    }

    const std::string needle = lower(luaL_checkstring(state, 1));
    const std::size_t count = api->player_count(api->context);
    for (std::size_t index = 0; index < count; ++index) {
        PlayerInfo player;
        if (!api->player_at(api->context, index, &player)) continue;
        if (lower(player.name ? player.name : "") == needle ||
            lower(player.steam_id ? player.steam_id : "") == needle) {
            push_player(state, api, player);
            return 1;
        }
    }
    lua_pushnil(state);
    return 1;
}

int all(lua_State* state) {
    const auto* api = services(state);
    if (!api || !api->player_count || !api->player_at) {
        lua_newtable(state);
        return 1;
    }
    const std::size_t count = api->player_count(api->context);
    const int capacity = luacs::lua_checked::checked_table_capacity(
        state, count, "player list is too large for a Lua table");
    lua_createtable(state, capacity, 0);
    lua_Integer output = 1;
    for (std::size_t index = 0; index < count; ++index) {
        PlayerInfo player;
        if (!api->player_at(api->context, index, &player)) continue;
        if (player.slot < 0 || player.slot >= 64) continue;
        push_player(state, api, player);
        lua_seti(state, -2, output++);
    }
    return 1;
}

int set_integer_field(lua_State* state, PlayerIntField field,
                      const char* table_field) {
    const auto* api = services(state);
    const int slot = slot_from(state, 1);
    const int value = luacs::lua_checked::checked_int(
        state, 2, std::numeric_limits<int>::min(),
        std::numeric_limits<int>::max(), "value must fit a 32-bit integer");
    char error[256]{};
    if (!api || !api->player_set_int ||
        !api->player_set_int(api->context, slot, field, value, error,
                             sizeof(error))) {
        return fail(state, error[0] ? error : "player mutation failed");
    }
    if (lua_istable(state, 1)) {
        set_int(state, 1, table_field, value);
    }
    lua_pushboolean(state, true);
    return 1;
}

int set_boolean_field(lua_State* state, PlayerBoolField field,
                      const char* table_field) {
    const auto* api = services(state);
    const int slot = slot_from(state, 1);
    const bool value = luacs::lua_checked::strict_boolean(state, 2);
    char error[256]{};
    if (!api || !api->player_set_bool ||
        !api->player_set_bool(api->context, slot, field, value, error,
                              sizeof(error))) {
        return fail(state, error[0] ? error : "player mutation failed");
    }
    if (lua_istable(state, 1) && table_field) {
        set_bool(state, 1, table_field, value);
    }
    lua_pushboolean(state, true);
    return 1;
}

int set_health(lua_State* state) {
    return set_integer_field(state, PlayerIntField::Health, "health");
}
int set_armor(lua_State* state) {
    return set_integer_field(state, PlayerIntField::Armor, "armor");
}
int set_money(lua_State* state) {
    return set_integer_field(state, PlayerIntField::Money, "money");
}
int set_helmet(lua_State* state) {
    return set_boolean_field(state, PlayerBoolField::Helmet, "helmet");
}
int set_defuser(lua_State* state) {
    return set_boolean_field(state, PlayerBoolField::Defuser, "defuser");
}
int set_prevent_weapon_pickup(lua_State* state) {
    return set_boolean_field(state, PlayerBoolField::PreventWeaponPickup,
                             nullptr);
}

bool read_vector(lua_State* state, int index, Vector3& output) {
    if (lua_isnoneornil(state, index)) return false;
    luaL_checktype(state, index, LUA_TTABLE);
    const int table = lua_absindex(state, index);
    lua_getfield(state, table, "x");
    output.x = static_cast<float>(luaL_checknumber(state, -1));
    lua_pop(state, 1);
    lua_getfield(state, table, "y");
    output.y = static_cast<float>(luaL_checknumber(state, -1));
    lua_pop(state, 1);
    lua_getfield(state, table, "z");
    output.z = static_cast<float>(luaL_checknumber(state, -1));
    lua_pop(state, 1);
    if (!finite_vector(output)) {
        luaL_argerror(state, index, "teleport vector components must be finite");
    }
    return true;
}

int teleport(lua_State* state) {
    const auto* api = services(state);
    const int slot = slot_from(state, 1);
    Vector3 position{};
    Vector3 angles{};
    Vector3 velocity{};
    const bool has_position = read_vector(state, 2, position);
    const bool has_angles = read_vector(state, 3, angles);
    const bool has_velocity = read_vector(state, 4, velocity);
    if (!has_position && !has_angles && !has_velocity) {
        return luaL_error(state,
                          "teleport requires position, angles, or velocity");
    }

    char error[256]{};
    if (!api || !api->player_teleport ||
        !api->player_teleport(api->context, slot,
                              has_position ? &position : nullptr,
                              has_angles ? &angles : nullptr,
                              has_velocity ? &velocity : nullptr, error,
                              sizeof(error))) {
        return fail(state, error[0] ? error : "teleport failed");
    }
    if (lua_istable(state, 1)) {
        if (has_position) {
            push_vector(state, position);
            lua_setfield(state, 1, "position");
        }
        if (has_angles) {
            push_vector(state, angles);
            lua_setfield(state, 1, "eye_angles");
        }
        if (has_velocity) {
            push_vector(state, velocity);
            lua_setfield(state, 1, "velocity");
        }
    }
    lua_pushboolean(state, true);
    return 1;
}

int kill(lua_State* state) {
    const auto* api = services(state);
    const int slot = slot_from(state, 1);
    const bool explode =
        luacs::lua_checked::optional_boolean(state, 2, false);
    char error[256]{};
    if (!api || !api->player_kill ||
        !api->player_kill(api->context, slot, explode, error,
                          sizeof(error))) {
        return fail(state, error[0] ? error : "kill failed");
    }
    if (lua_istable(state, 1)) {
        set_bool(state, 1, "alive", false);
        set_int(state, 1, "health", 0);
    }
    lua_pushboolean(state, true);
    return 1;
}

int respawn(lua_State* state) {
    const auto* api = services(state);
    const int slot = slot_from(state, 1);
    char error[256]{};
    if (!api || !api->player_respawn ||
        !api->player_respawn(api->context, slot, error, sizeof(error))) {
        return fail(state, error[0] ? error : "respawn failed");
    }
    lua_pushboolean(state, true);
    return 1;
}

int team_action(lua_State* state, bool switch_team) {
    const auto* api = services(state);
    const int slot = slot_from(state, 1);
    const int team = luacs::lua_checked::checked_int(
        state, 2, 0, 3, "team must be 0, 1, 2, or 3");
    char error[256]{};
    if (!api || !api->player_change_team ||
        !api->player_change_team(api->context, slot, team, switch_team, error,
                                 sizeof(error))) {
        return fail(state, error[0] ? error : "team change failed");
    }
    if (lua_istable(state, 1)) set_int(state, 1, "team", team);
    lua_pushboolean(state, true);
    return 1;
}

int change_team(lua_State* state) { return team_action(state, false); }
int switch_team(lua_State* state) { return team_action(state, true); }

int is_valid(lua_State* state) {
    const auto* api = services(state);
    const int slot = slot_from(state, 1);
    PlayerState value;
    char error[64]{};
    lua_pushboolean(state,
                    api && api->player_state &&
                        api->player_state(api->context, slot, &value, error,
                                          sizeof(error)) &&
                        valid_player_state(value));
    return 1;
}

int is_alive(lua_State* state) {
    const auto* api = services(state);
    const int slot = slot_from(state, 1);
    PlayerState value;
    char error[64]{};
    lua_pushboolean(state,
                    api && api->player_state &&
                        api->player_state(api->context, slot, &value, error,
                                          sizeof(error)) &&
                        valid_player_state(value) && value.alive);
    return 1;
}

int player_tostring(lua_State* state) {
    lua_getfield(state, 1, "name");
    const char* name = lua_tostring(state, -1);
    const std::string stable_name = name ? name : "";
    lua_pop(state, 1);
    lua_getfield(state, 1, "slot");
    const lua_Integer slot = luaL_checkinteger(state, -1);
    lua_pop(state, 1);
    lua_pushfstring(state, "Player(%I, %s)", slot, stable_name.c_str());
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
        return luaL_error(state, "LuaCS players ABI mismatch");
    }

    if (luaL_newmetatable(state, kPlayerMeta)) {
        lua_newtable(state);
        add_function(state, api, "refresh", &refresh);
        add_function(state, api, "is_valid", &is_valid);
        add_function(state, api, "is_alive", &is_alive);
        add_function(state, api, "set_health", &set_health);
        add_function(state, api, "set_armor", &set_armor);
        add_function(state, api, "set_money", &set_money);
        add_function(state, api, "set_helmet", &set_helmet);
        add_function(state, api, "set_defuser", &set_defuser);
        add_function(state, api, "set_prevent_weapon_pickup",
                     &set_prevent_weapon_pickup);
        add_function(state, api, "teleport", &teleport);
        add_function(state, api, "kill", &kill);
        add_function(state, api, "respawn", &respawn);
        add_function(state, api, "change_team", &change_team);
        add_function(state, api, "switch_team", &switch_team);
        lua_setfield(state, -2, "__index");
        add_function(state, api, "__tostring", &player_tostring);
    }
    lua_pop(state, 1);

    lua_createtable(state, 0, 24);
    add_function(state, api, "get_by_slot", &get_by_slot);
    add_function(state, api, "get", &get);
    add_function(state, api, "all", &all);
    add_function(state, api, "refresh", &refresh);
    add_function(state, api, "is_valid", &is_valid);
    add_function(state, api, "is_alive", &is_alive);
    add_function(state, api, "set_health", &set_health);
    add_function(state, api, "set_armor", &set_armor);
    add_function(state, api, "set_money", &set_money);
    add_function(state, api, "set_helmet", &set_helmet);
    add_function(state, api, "set_defuser", &set_defuser);
    add_function(state, api, "set_prevent_weapon_pickup",
                 &set_prevent_weapon_pickup);
    add_function(state, api, "teleport", &teleport);
    add_function(state, api, "kill", &kill);
    add_function(state, api, "respawn", &respawn);
    add_function(state, api, "change_team", &change_team);
    add_function(state, api, "switch_team", &switch_team);

    lua_pushinteger(state, 0);
    lua_setfield(state, -2, "TEAM_NONE");
    lua_pushinteger(state, 1);
    lua_setfield(state, -2, "TEAM_SPECTATOR");
    lua_pushinteger(state, 2);
    lua_setfield(state, -2, "TEAM_T");
    lua_pushinteger(state, 3);
    lua_setfield(state, -2, "TEAM_CT");
    return 1;
}