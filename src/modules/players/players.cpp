#include "luacs/module_api.h"

extern "C" {
#include "lauxlib.h"
}

#include <string>

namespace {
const luacs::Services* g_services = nullptr;

std::string steam3(std::uint64_t steam64) {
    constexpr std::uint64_t base = 76561197960265728ULL;
    return steam64 < base ? std::string{} : "[U:1:" + std::to_string(steam64 - base) + "]";
}

void push_player(lua_State* state, const luacs::PlayerInfo& player) {
    lua_createtable(state, 0, 9);
    lua_pushinteger(state, player.slot); lua_setfield(state, -2, "slot");
    lua_pushstring(state, player.name ? player.name : ""); lua_setfield(state, -2, "name");
    lua_pushinteger(state, static_cast<lua_Integer>(player.steam64)); lua_setfield(state, -2, "steam64");
    lua_pushstring(state, player.steam_id ? player.steam_id : ""); lua_setfield(state, -2, "steamid");
    const auto value = steam3(player.steam64);
    lua_pushlstring(state, value.data(), value.size()); lua_setfield(state, -2, "steam3");
    lua_pushboolean(state, player.fake); lua_setfield(state, -2, "fake");
    lua_pushboolean(state, player.connected); lua_setfield(state, -2, "connected");
    lua_pushboolean(state, player.active); lua_setfield(state, -2, "active");
}

int get_by_slot(lua_State* state) {
    const int slot = static_cast<int>(luaL_checkinteger(state, 1));
    luacs::PlayerInfo player;
    if (!g_services->player_get(g_services->context, slot, &player)) {
        lua_pushnil(state);
        return 1;
    }
    push_player(state, player);
    return 1;
}

int all(lua_State* state) {
    const auto count = g_services->player_count(g_services->context);
    lua_createtable(state, static_cast<int>(count), 0);
    for (std::size_t index = 0; index < count; ++index) {
        luacs::PlayerInfo player;
        if (!g_services->player_at(g_services->context, index, &player)) continue;
        push_player(state, player);
        lua_seti(state, -2, static_cast<lua_Integer>(index + 1));
    }
    return 1;
}
} // namespace

LUACS_MODULE_EXPORT int LuaCS_OpenModule(lua_State* state, const luacs::Services* services) {
    if (!services || services->abi_version != luacs::kModuleAbiVersion ||
        !services->player_get || !services->player_count || !services->player_at) {
        return luaL_error(state, "players.dll received an incompatible LuaCS service table");
    }
    g_services = services;
    lua_createtable(state, 0, 3);
    lua_pushcfunction(state, get_by_slot); lua_setfield(state, -2, "get_by_slot");
    lua_pushcfunction(state, get_by_slot); lua_setfield(state, -2, "get");
    lua_pushcfunction(state, all); lua_setfield(state, -2, "all");
    return 1;
}
