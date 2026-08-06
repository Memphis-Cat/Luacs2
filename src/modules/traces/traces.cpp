#include "luacs/advanced_world_module.h"
#include "luacs/module_api.h"

extern "C" {
#include "lauxlib.h"
}

#include <cstddef>
#include <cstdint>

namespace {
using luacs::AdvancedWorldServices;
using luacs::Services;
using luacs::TraceRequest;
using luacs::TraceResult;
using luacs::Vector3;

const AdvancedWorldServices* advanced() {
    return luacs::resolve_advanced_world_services();
}

int fail(lua_State* state, const char* error) {
    lua_pushnil(state);
    lua_pushstring(state, error && *error ? error : "trace operation failed");
    return 2;
}

Vector3 read_vector(lua_State* state, int index) {
    luaL_checktype(state, index, LUA_TTABLE);
    Vector3 value;
    lua_getfield(state, index, "x");
    value.x = static_cast<float>(luaL_checknumber(state, -1));
    lua_pop(state, 1);
    lua_getfield(state, index, "y");
    value.y = static_cast<float>(luaL_checknumber(state, -1));
    lua_pop(state, 1);
    lua_getfield(state, index, "z");
    value.z = static_cast<float>(luaL_checknumber(state, -1));
    lua_pop(state, 1);
    return value;
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

int entity_index_from(lua_State* state, int index) {
    if (lua_isnoneornil(state, index)) return -1;
    if (lua_isinteger(state, index)) {
        return static_cast<int>(lua_tointeger(state, index));
    }
    luaL_checktype(state, index, LUA_TTABLE);
    lua_getfield(state, index, "entity_index");
    const int result = static_cast<int>(luaL_checkinteger(state, -1));
    lua_pop(state, 1);
    return result;
}

std::uint64_t read_u64_field(lua_State* state, int table, const char* key,
                             std::uint64_t fallback) {
    lua_getfield(state, table, key);
    const std::uint64_t value = lua_isnil(state, -1)
                                    ? fallback
                                    : static_cast<std::uint64_t>(
                                          luaL_checkinteger(state, -1));
    lua_pop(state, 1);
    return value;
}

bool read_bool_field(lua_State* state, int table, const char* key,
                     bool fallback) {
    lua_getfield(state, table, key);
    const bool value = lua_isnil(state, -1) ? fallback
                                             : lua_toboolean(state, -1) != 0;
    lua_pop(state, 1);
    return value;
}

void push_result(lua_State* state, const TraceResult& result) {
    lua_createtable(state, 0, 22);
#define SET_BOOL(name)                                                         \
    lua_pushboolean(state, result.name);                                       \
    lua_setfield(state, -2, #name)
#define SET_INT(name)                                                          \
    lua_pushinteger(state, result.name);                                       \
    lua_setfield(state, -2, #name)
#define SET_NUM(name)                                                          \
    lua_pushnumber(state, result.name);                                        \
    lua_setfield(state, -2, #name)
    SET_BOOL(valid);
    SET_BOOL(hit);
    SET_BOOL(start_solid);
    SET_BOOL(all_solid);
    SET_NUM(fraction);
    SET_NUM(fraction_left_solid);
    SET_NUM(plane_distance);
    SET_INT(entity_index);
    lua_pushinteger(state, result.entity_handle);
    lua_setfield(state, -2, "entity_handle");
    SET_INT(hitbox);
    SET_INT(hitgroup);
    SET_INT(surface_flags);
    SET_INT(contents);
#undef SET_NUM
#undef SET_INT
#undef SET_BOOL
    push_vector(state, result.start);
    lua_setfield(state, -2, "start");
    push_vector(state, result.end);
    lua_setfield(state, -2, "end_position");
    push_vector(state, result.hit_position);
    lua_setfield(state, -2, "position");
    push_vector(state, result.plane_normal);
    lua_setfield(state, -2, "normal");
    lua_pushstring(state, result.surface_name);
    lua_setfield(state, -2, "surface_name");
}

int execute(lua_State* state, bool hull) {
    const auto* api = advanced();
    TraceRequest request;
    request.start = read_vector(state, 1);
    request.end = read_vector(state, 2);
    request.use_hull = hull;
    int options = hull ? 5 : 3;
    if (hull) {
        request.mins = read_vector(state, 3);
        request.maxs = read_vector(state, 4);
    }
    if (lua_istable(state, options)) {
        request.contents_mask =
            read_u64_field(state, options, "mask", request.contents_mask);
        request.collision_group =
            read_u64_field(state, options, "collision_group", 0);
        request.hit_triggers =
            read_bool_field(state, options, "hit_triggers", false);
        lua_getfield(state, options, "ignore");
        if (!lua_isnil(state, -1)) {
            request.ignore_entity_index = entity_index_from(state, -1);
        }
        lua_pop(state, 1);
    }
    TraceResult result;
    char error[256]{};
    if (!api || !api->trace ||
        !api->trace(api->context, &request, &result, error, sizeof(error))) {
        return fail(state, error[0] ? error : "trace service is unavailable");
    }
    push_result(state, result);
    return 1;
}

int ray(lua_State* state) { return execute(state, false); }
int hull(lua_State* state) { return execute(state, true); }
int line(lua_State* state) { return execute(state, false); }

void add_function(lua_State* state, const Services* api, const char* name,
                  lua_CFunction function) {
    lua_pushlightuserdata(state, const_cast<Services*>(api));
    lua_pushcclosure(state, function, 1);
    lua_setfield(state, -2, name);
}

void add_mask(lua_State* state, const char* name, std::uint64_t value) {
    lua_pushinteger(state, static_cast<lua_Integer>(value));
    lua_setfield(state, -2, name);
}
} // namespace

LUACS_MODULE_EXPORT int LuaCS_OpenModule(lua_State* state,
                                         const Services* api) {
    if (!api || api->abi_version != luacs::kModuleAbiVersion) {
        return luaL_error(state, "LuaCS traces ABI mismatch");
    }
    if (!advanced()) {
        return luaL_error(state, "LuaCS advanced world services are unavailable");
    }
    lua_createtable(state, 0, 24);
    add_function(state, api, "ray", &ray);
    add_function(state, api, "line", &line);
    add_function(state, api, "hull", &hull);
    add_mask(state, "MASK_ALL", 0xFFFFFFFFFFFFFFFFull);
    add_mask(state, "MASK_SOLID", 0x1ull);
    add_mask(state, "MASK_PLAYERSOLID", 0x200400Bull);
    add_mask(state, "MASK_SHOT", 0x4600400Bull);
    add_mask(state, "MASK_SHOT_HULL", 0x600400Bull);
    add_mask(state, "MASK_VISIBLE", 0x46004003ull);
    add_mask(state, "MASK_WATER", 0x20ull);
    return 1;
}
