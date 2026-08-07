#include "luacs/lua_checked.h"
#include "luacs/module_api.h"

extern "C" {
#include "lauxlib.h"
}

#include <algorithm>
#include <cerrno>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <string>
#include <string_view>

namespace {

const luacs::Services* g_services = nullptr;

int push_error(lua_State* state, const char* error) {
    lua_pushnil(state);
    lua_pushstring(state, error && error[0] ? error : "cvar operation failed");
    return 2;
}

bool read_value(const char* name, std::string& value, std::string& error) {
    char output[4096]{};
    char error_buffer[512]{};
    if (!g_services->cvar_get(g_services->context, name, output, sizeof(output),
                              error_buffer, sizeof(error_buffer))) {
        error = error_buffer[0] ? error_buffer : "cvar read failed";
        return false;
    }
    value = output;
    return true;
}

int exists(lua_State* state) {
    const char* name = luaL_checkstring(state, 1);
    lua_pushboolean(state, g_services->cvar_exists(g_services->context, name));
    return 1;
}

int get(lua_State* state) {
    const char* name = luaL_checkstring(state, 1);
    std::string value;
    std::string error;
    if (!read_value(name, value, error)) return push_error(state, error.c_str());
    lua_pushlstring(state, value.data(), value.size());
    return 1;
}

int set(lua_State* state) {
    const char* name = luaL_checkstring(state, 1);
    std::size_t length = 0;
    const char* value = luaL_tolstring(state, 2, &length);
    if (!value) return luaL_argerror(state, 2, "cvar value cannot be converted to text");
    if (std::string_view(value, length).find('\0') != std::string_view::npos) {
        lua_pop(state, 1);
        return luaL_argerror(state, 2, "cvar value cannot contain NUL bytes");
    }
    const std::string stable_value(value, length);
    lua_pop(state, 1);

    char error[512]{};
    if (!g_services->cvar_set(g_services->context, name, stable_value.c_str(),
                              error, sizeof(error))) {
        return push_error(state, error);
    }
    lua_pushboolean(state, 1);
    return 1;
}

std::string lower_ascii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](char ch) {
        if (ch >= 'A' && ch <= 'Z') return static_cast<char>(ch - 'A' + 'a');
        return ch;
    });
    return value;
}

int get_bool(lua_State* state) {
    const char* name = luaL_checkstring(state, 1);
    std::string value;
    std::string error;
    if (!read_value(name, value, error)) return push_error(state, error.c_str());

    value = lower_ascii(std::move(value));
    if (value == "1" || value == "true" || value == "yes" || value == "on") {
        lua_pushboolean(state, 1);
        return 1;
    }
    if (value == "0" || value == "false" || value == "no" || value == "off") {
        lua_pushboolean(state, 0);
        return 1;
    }
    const std::string message = "cvar '" + std::string(name) +
                                "' is not a boolean value";
    return push_error(state, message.c_str());
}

int get_int(lua_State* state) {
    const char* name = luaL_checkstring(state, 1);
    std::string value;
    std::string error;
    if (!read_value(name, value, error)) return push_error(state, error.c_str());

    lua_Integer parsed = 0;
    const char* begin = value.data();
    const char* end = begin + value.size();
    const auto result = std::from_chars(begin, end, parsed);
    if (begin == end || result.ec != std::errc{} || result.ptr != end) {
        const std::string message = "cvar '" + std::string(name) +
                                    "' is not an integer value";
        return push_error(state, message.c_str());
    }
    lua_pushinteger(state, parsed);
    return 1;
}

int get_number(lua_State* state) {
    const char* name = luaL_checkstring(state, 1);
    std::string value;
    std::string error;
    if (!read_value(name, value, error)) return push_error(state, error.c_str());

    if (value.empty()) {
        const std::string message = "cvar '" + std::string(name) +
                                    "' is not a numeric value";
        return push_error(state, message.c_str());
    }

    errno = 0;
    char* tail = nullptr;
    const double parsed = std::strtod(value.c_str(), &tail);
    if (errno == ERANGE || tail == value.c_str() ||
        tail != value.c_str() + value.size() || !std::isfinite(parsed)) {
        const std::string message = "cvar '" + std::string(name) +
                                    "' is not a finite numeric value";
        return push_error(state, message.c_str());
    }
    lua_pushnumber(state, parsed);
    return 1;
}

void add_function(lua_State* state, const char* name, lua_CFunction function) {
    lua_pushcfunction(state, function);
    lua_setfield(state, -2, name);
}

} // namespace

LUACS_MODULE_EXPORT int LuaCS_OpenModule(lua_State* state,
                                         const luacs::Services* services) {
    if (!services || services->abi_version != luacs::kModuleAbiVersion ||
        !services->cvar_exists || !services->cvar_get || !services->cvar_set) {
        return luaL_error(state,
                          "cvars.dll received an incompatible LuaCS service table");
    }
    g_services = services;

    lua_createtable(state, 0, 8);
    add_function(state, "exists", exists);
    add_function(state, "get", get);
    add_function(state, "get_string", get);
    add_function(state, "set", set);
    add_function(state, "set_string", set);
    add_function(state, "get_bool", get_bool);
    add_function(state, "get_int", get_int);
    add_function(state, "get_number", get_number);
    return 1;
}
