#pragma once

extern "C" {
#include "lauxlib.h"
}

#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>

namespace luacs::lua_checked {

inline int checked_int(lua_State* state, int index, int minimum, int maximum,
                       const char* message) {
    const lua_Integer raw = luaL_checkinteger(state, index);
    if (raw < static_cast<lua_Integer>(minimum) ||
        raw > static_cast<lua_Integer>(maximum)) {
        luaL_argerror(state, index, message);
    }
    return static_cast<int>(raw);
}

inline int checked_nonnegative_int(lua_State* state, int index,
                                   const char* message) {
    return checked_int(state, index, 0, std::numeric_limits<int>::max(),
                       message);
}

inline int checked_slot(lua_State* state, int index) {
    return checked_int(state, index, 0, 63,
                       "player slot must be between 0 and 63");
}

inline int checked_entity_index(lua_State* state, int index,
                                bool allow_negative_one = false) {
    const int minimum = allow_negative_one ? -1 : 0;
    return checked_int(
        state, index, minimum, std::numeric_limits<int>::max(),
        allow_negative_one
            ? "entity index must be -1 or a non-negative 32-bit integer"
            : "entity index must be a non-negative 32-bit integer");
}

inline bool strict_boolean(lua_State* state, int index) {
    luaL_checktype(state, index, LUA_TBOOLEAN);
    return lua_toboolean(state, index) != 0;
}

inline bool optional_boolean(lua_State* state, int index, bool fallback) {
    if (lua_isnoneornil(state, index)) return fallback;
    return strict_boolean(state, index);
}

inline double finite_number(lua_State* state, int index,
                            const char* message) {
    const double value = luaL_checknumber(state, index);
    if (!std::isfinite(value)) luaL_argerror(state, index, message);
    return value;
}

inline float finite_float(lua_State* state, int index, const char* message) {
    const double value = finite_number(state, index, message);
    if (value < -static_cast<double>(std::numeric_limits<float>::max()) ||
        value > static_cast<double>(std::numeric_limits<float>::max())) {
        luaL_argerror(state, index, message);
    }
    return static_cast<float>(value);
}

inline int checked_table_capacity(lua_State* state, std::size_t count,
                                  const char* message) {
    if (count > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        luaL_error(state, "%s", message);
    }
    return static_cast<int>(count);
}

inline void push_u64_exact(lua_State* state, std::uint64_t value) {
    if (value <= static_cast<std::uint64_t>(
                     std::numeric_limits<lua_Integer>::max())) {
        lua_pushinteger(state, static_cast<lua_Integer>(value));
        return;
    }
    const std::string exact = std::to_string(value);
    lua_pushlstring(state, exact.data(), exact.size());
}

inline std::uint64_t read_u64_exact(lua_State* state, int index,
                                    const char* label) {
    if (lua_isinteger(state, index)) {
        const lua_Integer raw = lua_tointeger(state, index);
        if (raw < 0) {
            luaL_argerror(state, index,
                          "unsigned 64-bit value cannot be negative");
        }
        return static_cast<std::uint64_t>(raw);
    }

    std::size_t size = 0;
    const char* raw = luaL_checklstring(state, index, &size);
    std::string_view text(raw ? raw : "", size);
    if (text.empty() || text.front() == '-' || text.front() == '+') {
        std::string message = "invalid ";
        message += label;
        message += "; expected a non-negative integer or decimal/0x string";
        luaL_argerror(state, index, message.c_str());
    }

    int base = 10;
    if (text.size() > 2 && text[0] == '0' &&
        (text[1] == 'x' || text[1] == 'X')) {
        base = 16;
        text.remove_prefix(2);
        if (text.empty()) {
            std::string message = "invalid ";
            message += label;
            message += "; hexadecimal value has no digits";
            luaL_argerror(state, index, message.c_str());
        }
    }

    std::uint64_t value = 0;
    const auto result =
        std::from_chars(text.data(), text.data() + text.size(), value, base);
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size()) {
        std::string message = "invalid ";
        message += label;
        message += "; expected an exact unsigned 64-bit decimal/0x value";
        luaL_argerror(state, index, message.c_str());
    }
    return value;
}

inline std::uint32_t checked_u32(lua_State* state, int index,
                                 const char* message) {
    const lua_Integer raw = luaL_checkinteger(state, index);
    if (raw < 0 ||
        static_cast<std::uint64_t>(raw) >
            std::numeric_limits<std::uint32_t>::max()) {
        luaL_argerror(state, index, message);
    }
    return static_cast<std::uint32_t>(raw);
}

} // namespace luacs::lua_checked
