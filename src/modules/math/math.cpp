#include "luacs/lua_checked.h"
#include "luacs/module_api.h"

extern "C" {
#include "lauxlib.h"
}

#include <cmath>

namespace {

void read_vector(lua_State* state, int index, double& x, double& y, double& z) {
    luaL_checktype(state, index, LUA_TTABLE);
    const int table = lua_absindex(state, index);

    lua_getfield(state, table, "x");
    x = luacs::lua_checked::finite_number(
        state, -1, "vector x component must be finite");
    lua_pop(state, 1);
    lua_getfield(state, table, "y");
    y = luacs::lua_checked::finite_number(
        state, -1, "vector y component must be finite");
    lua_pop(state, 1);
    lua_getfield(state, table, "z");
    z = luacs::lua_checked::finite_number(
        state, -1, "vector z component must be finite");
    lua_pop(state, 1);
}

int distance(lua_State* state) {
    double ax = 0.0;
    double ay = 0.0;
    double az = 0.0;
    double bx = 0.0;
    double by = 0.0;
    double bz = 0.0;
    read_vector(state, 1, ax, ay, az);
    read_vector(state, 2, bx, by, bz);
    const double dx = bx - ax;
    const double dy = by - ay;
    const double dz = bz - az;
    const double result = std::hypot(dx, dy, dz);
    if (!std::isfinite(result)) {
        return luaL_error(state, "vector distance overflowed to a non-finite value");
    }
    lua_pushnumber(state, result);
    return 1;
}

int clamp(lua_State* state) {
    const double value = luacs::lua_checked::finite_number(
        state, 1, "value must be finite");
    const double minimum = luacs::lua_checked::finite_number(
        state, 2, "minimum must be finite");
    const double maximum = luacs::lua_checked::finite_number(
        state, 3, "maximum must be finite");
    if (minimum > maximum) {
        return luaL_error(state, "minimum cannot be greater than maximum");
    }
    lua_pushnumber(state,
                   value < minimum ? minimum : value > maximum ? maximum : value);
    return 1;
}

} // namespace

LUACS_MODULE_EXPORT int LuaCS_OpenModule(lua_State* state,
                                         const luacs::Services* services) {
    if (!services || services->abi_version != luacs::kModuleAbiVersion) {
        return luaL_error(state,
                          "math.dll received an incompatible LuaCS service table");
    }
    lua_createtable(state, 0, 2);
    lua_pushcfunction(state, distance);
    lua_setfield(state, -2, "distance");
    lua_pushcfunction(state, clamp);
    lua_setfield(state, -2, "clamp");
    return 1;
}
