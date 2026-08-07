#include "luacs/lua_checked.h"
#include "luacs/module_api.h"
#include "luacs/world_module.h"

extern "C" {
#include "lauxlib.h"
}

#include <cstdint>
#include <limits>

namespace {

using luacs::PlayerInfo;
using luacs::PlayerState;
using luacs::Services;
using luacs::WorldServices;

inline constexpr const char* kPlayerMeta = "LuaCS.Player";

const Services* services(lua_State* state) {
    return static_cast<const Services*>(
        lua_touserdata(state, lua_upvalueindex(1)));
}

const WorldServices* world(lua_State* state) {
    return luacs::resolve_world_services(services(state));
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

int check_team(lua_State* state, int index, bool playable_only = false) {
    const int minimum = playable_only ? 1 : 0;
    return luacs::lua_checked::checked_int(
        state, index, minimum, 3,
        playable_only ? "team must be SPECTATOR, T, or CT"
                      : "team must be NONE, SPECTATOR, T, or CT");
}

int fail(lua_State* state, const char* error) {
    lua_pushnil(state);
    lua_pushstring(state, error && *error ? error : "team operation failed");
    return 2;
}

void push_player(lua_State* state, const PlayerInfo& player,
                 const PlayerState* live) {
    lua_createtable(state, 0, 24);
    lua_pushinteger(state, player.slot);
    lua_setfield(state, -2, "slot");
    lua_pushstring(state, player.name ? player.name : "");
    lua_setfield(state, -2, "name");
    luacs::lua_checked::push_u64_exact(state, player.steam64);
    lua_setfield(state, -2, "steam64");
    lua_pushstring(state, player.steam_id ? player.steam_id : "");
    lua_setfield(state, -2, "steamid");
    lua_pushboolean(state, player.fake);
    lua_setfield(state, -2, "fake");
    lua_pushboolean(state, player.connected);
    lua_setfield(state, -2, "connected");
    lua_pushboolean(state, player.active);
    lua_setfield(state, -2, "active");

    if (live) {
        lua_pushboolean(state, live->valid);
        lua_setfield(state, -2, "valid");
        lua_pushboolean(state, live->alive);
        lua_setfield(state, -2, "alive");
        lua_pushinteger(state, live->team);
        lua_setfield(state, -2, "team");
        lua_pushinteger(state, live->health);
        lua_setfield(state, -2, "health");
        lua_pushinteger(state, live->armor);
        lua_setfield(state, -2, "armor");
        lua_pushinteger(state, live->pawn_index);
        lua_setfield(state, -2, "pawn_index");
    }

    luaL_getmetatable(state, kPlayerMeta);
    if (lua_istable(state, -1)) {
        lua_setmetatable(state, -2);
    } else {
        lua_pop(state, 1);
    }
}

int change_impl(lua_State* state, bool switch_team) {
    const auto* api = services(state);
    const int slot = slot_from(state, 1);
    const int team = check_team(state, 2, true);
    char error[256]{};
    if (!api || !api->player_change_team ||
        !api->player_change_team(api->context, slot, team, switch_team, error,
                                 sizeof(error))) {
        return fail(state,
                    error[0] ? error : "player team service is unavailable");
    }
    if (lua_istable(state, 1)) {
        lua_pushinteger(state, team);
        lua_setfield(state, 1, "team");
    }
    lua_pushboolean(state, true);
    return 1;
}

int change(lua_State* state) { return change_impl(state, false); }
int switch_team(lua_State* state) { return change_impl(state, true); }

int get_players(lua_State* state) {
    const auto* api = services(state);
    const int team = check_team(state, 1);
    if (!api || !api->player_count || !api->player_at || !api->player_state) {
        return fail(state, "player state service is unavailable");
    }

    lua_newtable(state);
    lua_Integer output_index = 1;
    const std::size_t count = api->player_count(api->context);
    for (std::size_t index = 0; index < count; ++index) {
        PlayerInfo player;
        if (!api->player_at(api->context, index, &player)) continue;
        if (player.slot < 0 || player.slot >= 64) continue;
        PlayerState live;
        char error[256]{};
        if (!api->player_state(api->context, player.slot, &live, error,
                               sizeof(error)) ||
            !live.valid || live.team != team) {
            continue;
        }
        push_player(state, player, &live);
        lua_seti(state, -2, output_index++);
    }
    return 1;
}

int get_score(lua_State* state) {
    const auto* api = world(state);
    const int team = check_team(state, 1, true);
    int score = 0;
    char error[256]{};
    if (!api || !api->team_get_score ||
        !api->team_get_score(api->context, team, &score, error,
                             sizeof(error))) {
        return fail(state,
                    error[0] ? error : "team score service is unavailable");
    }
    if (score < 0) return fail(state, "Source 2 returned a negative team score");
    lua_pushinteger(state, score);
    return 1;
}

int set_score(lua_State* state) {
    const auto* api = world(state);
    const int team = check_team(state, 1, true);
    const int score = luacs::lua_checked::checked_int(
        state, 2, 0, std::numeric_limits<int>::max(),
        "score must be a non-negative 32-bit integer");
    char error[256]{};
    if (!api || !api->team_set_score ||
        !api->team_set_score(api->context, team, score, error,
                             sizeof(error))) {
        return fail(state,
                    error[0] ? error : "team score service is unavailable");
    }
    lua_pushboolean(state, true);
    return 1;
}

int add_score(lua_State* state) {
    const auto* api = world(state);
    const int team = check_team(state, 1, true);
    const int delta = luacs::lua_checked::checked_int(
        state, 2, std::numeric_limits<int>::min(),
        std::numeric_limits<int>::max(), "score delta must fit a 32-bit integer");
    int score = 0;
    char error[256]{};
    if (!api || !api->team_get_score || !api->team_set_score ||
        !api->team_get_score(api->context, team, &score, error,
                             sizeof(error))) {
        return fail(state,
                    error[0] ? error : "team score service is unavailable");
    }
    if (score < 0) return fail(state, "Source 2 returned a negative team score");

    const std::int64_t result = static_cast<std::int64_t>(score) +
                                static_cast<std::int64_t>(delta);
    if (result < 0 || result > std::numeric_limits<int>::max()) {
        return luaL_argerror(
            state, 2,
            "resulting score must be between 0 and INT_MAX");
    }
    const int new_score = static_cast<int>(result);
    if (!api->team_set_score(api->context, team, new_score, error,
                             sizeof(error))) {
        return fail(state, error[0] ? error : "team score update failed");
    }
    lua_pushinteger(state, new_score);
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
        return luaL_error(state, "LuaCS teams ABI mismatch");
    }
    if (!luacs::resolve_world_services(api)) {
        return luaL_error(state, "LuaCS world services are unavailable");
    }

    lua_createtable(state, 0, 16);
    add_function(state, api, "change", &change);
    add_function(state, api, "switch", &switch_team);
    add_function(state, api, "get_players", &get_players);
    add_function(state, api, "get_score", &get_score);
    add_function(state, api, "set_score", &set_score);
    add_function(state, api, "add_score", &add_score);

    lua_pushinteger(state, 0);
    lua_setfield(state, -2, "NONE");
    lua_pushinteger(state, 1);
    lua_setfield(state, -2, "SPECTATOR");
    lua_pushinteger(state, 2);
    lua_setfield(state, -2, "T");
    lua_pushinteger(state, 2);
    lua_setfield(state, -2, "TERRORIST");
    lua_pushinteger(state, 3);
    lua_setfield(state, -2, "CT");
    lua_pushinteger(state, 3);
    lua_setfield(state, -2, "COUNTER_TERRORIST");
    return 1;
}
