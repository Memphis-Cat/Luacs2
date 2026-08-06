extern "C" {
#include "lauxlib.h"
#include "lua.h"
}

namespace {

void luacs_replace_or_extend(lua_State* state, int index) {
    const int top = lua_gettop(state);
    const int absolute = lua_absindex(state, index);

    if (absolute == top + 1) {
        // Move the current top value into the next stack slot while leaving a
        // nil placeholder in its old slot. This is the exact layout needed by
        // set_velocity(entity, vector): { entity, nil, nil, vector }.
        lua_pushnil(state);
        lua_copy(state, top, absolute);
        lua_pushnil(state);
        lua_copy(state, -1, top);
        lua_pop(state, 1);
        return;
    }

    if (absolute <= 0 || absolute > top) {
        luaL_error(state, "invalid Lua stack index %d for replacement", index);
        return;
    }

    lua_copy(state, -1, absolute);
    lua_pop(state, 1);
}

} // namespace

#undef lua_replace
#define lua_replace luacs_replace_or_extend
#include "entities.cpp"
#undef lua_replace
