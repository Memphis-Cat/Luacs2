#include "luacs/module_api.h"

extern "C" {
#include "lauxlib.h"
}

#include <cmath>

namespace {
void read_vector(lua_State* state, int index, double& x, double& y, double& z) {
    luaL_checktype(state, index, LUA_TTABLE);
    lua_getfield(state, index, "x"); x = luaL_checknumber(state, -1); lua_pop(state, 1);
    lua_getfield(state, index, "y"); y = luaL_checknumber(state, -1); lua_pop(state, 1);
    lua_getfield(state, index, "z"); z = luaL_checknumber(state, -1); lua_pop(state, 1);
}
int distance(lua_State* state) {
    double ax, ay, az, bx, by, bz;
    read_vector(state, 1, ax, ay, az);
    read_vector(state, 2, bx, by, bz);
    const double dx = bx - ax, dy = by - ay, dz = bz - az;
    lua_pushnumber(state, std::sqrt(dx * dx + dy * dy + dz * dz));
    return 1;
}
int clamp(lua_State* state) {
    const double value = luaL_checknumber(state, 1);
    const double minimum = luaL_checknumber(state, 2);
    const double maximum = luaL_checknumber(state, 3);
    if (minimum > maximum) return luaL_error(state, "minimum cannot be greater than maximum");
    lua_pushnumber(state, value < minimum ? minimum : value > maximum ? maximum : value);
    return 1;
}
} // namespace

LUACS_MODULE_EXPORT int LuaCS_OpenModule(lua_State* state, const luacs::Services* services) {
    if (!services || services->abi_version != luacs::kModuleAbiVersion) {
        return luaL_error(state, "math.dll received an incompatible LuaCS service table");
    }
    lua_createtable(state, 0, 2);
    lua_pushcfunction(state, distance); lua_setfield(state, -2, "distance");
    lua_pushcfunction(state, clamp); lua_setfield(state, -2, "clamp");
    return 1;
}
