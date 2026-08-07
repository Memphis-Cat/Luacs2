#include "luacs/lua_checked.h"
#include "luacs/module_api.h"
#include "luacs/world_module.h"

extern "C" {
#include "lauxlib.h"
}

#include <cmath>

namespace {

using luacs::RoundState;
using luacs::Services;
using luacs::WorldServices;

const Services* services(lua_State* state) {
    return static_cast<const Services*>(
        lua_touserdata(state, lua_upvalueindex(1)));
}

const WorldServices* world(lua_State* state) {
    return luacs::resolve_world_services(services(state));
}

int fail(lua_State* state, const char* error) {
    lua_pushnil(state);
    lua_pushstring(state, error && *error ? error : "round operation failed");
    return 2;
}

bool read_state(lua_State* state, RoundState& output, char* error,
                std::size_t error_size) {
    const auto* api = world(state);
    return api && api->round_state &&
           api->round_state(api->context, &output, error, error_size);
}

bool valid_state(const RoundState& value) {
    return value.valid && std::isfinite(value.restart_time);
}

void push_state(lua_State* state, const RoundState& value) {
    lua_createtable(state, 0, 7);
    lua_pushboolean(state, value.valid);
    lua_setfield(state, -2, "valid");
    lua_pushboolean(state, value.frozen);
    lua_setfield(state, -2, "frozen");
    lua_pushinteger(state, value.number);
    lua_setfield(state, -2, "number");
    lua_pushinteger(state, value.win_status);
    lua_setfield(state, -2, "win_status");
    lua_pushinteger(state, value.win_reason);
    lua_setfield(state, -2, "win_reason");
    lua_pushnumber(state, value.restart_time);
    lua_setfield(state, -2, "restart_time");
}

bool get_valid_state(lua_State* state, RoundState& value, char* error,
                     std::size_t error_size) {
    if (!read_state(state, value, error, error_size)) return false;
    if (!valid_state(value)) {
        if (error && error_size) {
            std::snprintf(error, error_size, "%s",
                          "Source 2 returned invalid round state");
        }
        return false;
    }
    return true;
}

int get_state(lua_State* state) {
    RoundState value;
    char error[256]{};
    if (!get_valid_state(state, value, error, sizeof(error))) {
        return fail(state, error);
    }
    push_state(state, value);
    return 1;
}

int get_number(lua_State* state) {
    RoundState value;
    char error[256]{};
    if (!get_valid_state(state, value, error, sizeof(error))) {
        return fail(state, error);
    }
    lua_pushinteger(state, value.number);
    return 1;
}

int is_frozen(lua_State* state) {
    RoundState value;
    char error[256]{};
    if (!get_valid_state(state, value, error, sizeof(error))) {
        return fail(state, error);
    }
    lua_pushboolean(state, value.frozen);
    return 1;
}

int restart(lua_State* state) {
    const auto* api = world(state);
    const float delay = lua_isnoneornil(state, 1)
                            ? 1.0f
                            : luacs::lua_checked::finite_float(
                                  state, 1,
                                  "restart delay must be finite");
    if (delay < 0.0f || delay > 3600.0f) {
        return luaL_argerror(state, 1,
                             "restart delay must be between 0 and 3600 seconds");
    }
    char error[256]{};
    if (!api || !api->round_restart ||
        !api->round_restart(api->context, delay, error, sizeof(error))) {
        return fail(state,
                    error[0] ? error : "round restart service is unavailable");
    }
    lua_pushboolean(state, true);
    return 1;
}

int terminate(lua_State* state) {
    const auto* api = world(state);
    const int reason = luacs::lua_checked::checked_int(
        state, 1, 0, 255, "round-end reason must fit in one byte");
    const float delay = lua_isnoneornil(state, 2)
                            ? 0.0f
                            : luacs::lua_checked::finite_float(
                                  state, 2,
                                  "termination delay must be finite");
    if (delay < 0.0f || delay > 3600.0f) {
        return luaL_argerror(state, 2,
                             "termination delay must be between 0 and 3600 seconds");
    }
    char error[256]{};
    if (!api || !api->round_terminate ||
        !api->round_terminate(api->context, delay, reason, error,
                              sizeof(error))) {
        return fail(state, error[0] ? error
                                    : "round termination service is unavailable");
    }
    lua_pushboolean(state, true);
    return 1;
}

int set_frozen(lua_State* state, bool frozen) {
    const auto* api = world(state);
    char error[256]{};
    if (!api || !api->round_set_frozen ||
        !api->round_set_frozen(api->context, frozen, error, sizeof(error))) {
        return fail(state,
                    error[0] ? error : "round freeze service is unavailable");
    }
    lua_pushboolean(state, true);
    return 1;
}

int freeze(lua_State* state) { return set_frozen(state, true); }
int unfreeze(lua_State* state) { return set_frozen(state, false); }

void add_function(lua_State* state, const Services* api, const char* name,
                  lua_CFunction function) {
    lua_pushlightuserdata(state, const_cast<Services*>(api));
    lua_pushcclosure(state, function, 1);
    lua_setfield(state, -2, name);
}

void add_constant(lua_State* state, const char* name, int value) {
    lua_pushinteger(state, value);
    lua_setfield(state, -2, name);
}

} // namespace

LUACS_MODULE_EXPORT int LuaCS_OpenModule(lua_State* state,
                                         const Services* api) {
    if (!api || api->abi_version != luacs::kModuleAbiVersion) {
        return luaL_error(state, "LuaCS rounds ABI mismatch");
    }
    if (!luacs::resolve_world_services(api)) {
        return luaL_error(state, "LuaCS world services are unavailable");
    }

    lua_createtable(state, 0, 36);
    add_function(state, api, "state", &get_state);
    add_function(state, api, "get", &get_state);
    add_function(state, api, "get_number", &get_number);
    add_function(state, api, "is_frozen", &is_frozen);
    add_function(state, api, "restart", &restart);
    add_function(state, api, "terminate", &terminate);
    add_function(state, api, "freeze", &freeze);
    add_function(state, api, "unfreeze", &unfreeze);

    add_constant(state, "UNKNOWN", 0);
    add_constant(state, "TARGET_BOMBED", 1);
    add_constant(state, "TERRORISTS_ESCAPED", 4);
    add_constant(state, "CTS_PREVENT_ESCAPE", 5);
    add_constant(state, "ESCAPING_TERRORISTS_NEUTRALIZED", 6);
    add_constant(state, "BOMB_DEFUSED", 7);
    add_constant(state, "CT_WIN", 8);
    add_constant(state, "CTS_WIN", 8);
    add_constant(state, "T_WIN", 9);
    add_constant(state, "TERRORISTS_WIN", 9);
    add_constant(state, "DRAW", 10);
    add_constant(state, "ROUND_DRAW", 10);
    add_constant(state, "ALL_HOSTAGES_RESCUED", 11);
    add_constant(state, "TARGET_SAVED", 12);
    add_constant(state, "HOSTAGES_NOT_RESCUED", 13);
    add_constant(state, "TERRORISTS_NOT_ESCAPED", 14);
    add_constant(state, "GAME_COMMENCING", 16);
    add_constant(state, "TERRORISTS_SURRENDER", 17);
    add_constant(state, "CTS_SURRENDER", 18);
    add_constant(state, "TERRORISTS_PLANTED", 19);
    add_constant(state, "CTS_REACHED_HOSTAGE", 20);
    add_constant(state, "SURVIVAL_WIN", 21);
    add_constant(state, "SURVIVAL_DRAW", 22);
    return 1;
}
