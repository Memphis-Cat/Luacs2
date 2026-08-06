// Extend the complete trace module without duplicating its implementation.
#define LuaCS_OpenModule LuaCS_OpenModule_Base
#include "traces.cpp"
#undef LuaCS_OpenModule

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
    return 1;
}
