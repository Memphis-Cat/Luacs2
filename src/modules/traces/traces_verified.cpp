// Extend the complete trace module without duplicating its implementation.
#include <cstdio>

#define LuaCS_OpenModule LuaCS_OpenModule_Base
#include "traces.cpp"
#undef LuaCS_OpenModule

namespace {

int verified_trace_result_tostring(lua_State* state) {
    luaL_checktype(state, 1, LUA_TTABLE);
    lua_getfield(state, 1, "shape_name");
    const char* shape = luaL_checkstring(state, -1);
    lua_pop(state, 1);
    lua_getfield(state, 1, "fraction");
    const double fraction = static_cast<double>(luaL_checknumber(state, -1));
    lua_pop(state, 1);
    char buffer[160]{};
    std::snprintf(buffer, sizeof(buffer),
                  "TraceResult(%s, hit=%s, fraction=%.4f)", shape,
                  result_bool(state, "hit") ? "true" : "false", fraction);
    lua_pushstring(state, buffer);
    return 1;
}

void add_shape_map(lua_State* state) {
    lua_createtable(state, 0, 5);
    add_mask(state, "line", static_cast<std::uint64_t>(TraceShape::Line));
    add_mask(state, "sphere", static_cast<std::uint64_t>(TraceShape::Sphere));
    add_mask(state, "hull", static_cast<std::uint64_t>(TraceShape::Hull));
    add_mask(state, "capsule", static_cast<std::uint64_t>(TraceShape::Capsule));
    add_mask(state, "mesh", static_cast<std::uint64_t>(TraceShape::Mesh));
    lua_setfield(state, -2, "shapes");
}

void add_object_map(lua_State* state) {
    lua_createtable(state, 0, 6);
    add_mask(state, "static", 0x1ull);
    add_mask(state, "keyframed", 0x2ull);
    add_mask(state, "dynamic", 0x4ull);
    add_mask(state, "locatable", 0x8ull);
    add_mask(state, "all_game_entities", 0xEull);
    add_mask(state, "all", 0xFull);
    lua_setfield(state, -2, "objects");
}

} // namespace

LUACS_MODULE_EXPORT int LuaCS_OpenModule(lua_State* state,
                                         const Services* api) {
    const int results = LuaCS_OpenModule_Base(state, api);
    if (results != 1 || !lua_istable(state, -1)) return results;

    add_mask(state, "MAX_IGNORE_ENTITIES",
             static_cast<std::uint64_t>(luacs::kTraceIgnoreCapacity));
    add_mask(state, "MAX_MESH_VERTICES",
             static_cast<std::uint64_t>(luacs::kTraceMeshVertexCapacity));

    // Pinned Source 2 RnQueryObjectSet values from the CS2 HL2SDK.
    add_mask(state, "OBJECTS_STATIC", 0x1ull);
    add_mask(state, "OBJECTS_KEYFRAMED", 0x2ull);
    add_mask(state, "OBJECTS_DYNAMIC", 0x4ull);
    add_mask(state, "OBJECTS_LOCATABLE", 0x8ull);
    add_mask(state, "OBJECTS_ALL_GAME_ENTITIES", 0xEull);
    add_mask(state, "OBJECTS_ALL", 0xFull);
    add_shape_map(state);
    add_object_map(state);

    luaL_getmetatable(state, kTraceResultMeta);
    if (lua_istable(state, -1)) {
        add_function(state, api, "__tostring", &verified_trace_result_tostring);
    }
    lua_pop(state, 1);
    return 1;
}
