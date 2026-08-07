#pragma once

#include <cctype>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

namespace luacs::detail {

inline constexpr const char* kRuntimeRegistryKey = "LuaCS.Runtime";
inline constexpr const char* kVmRegistryKey = "LuaCS.ScriptVm";
inline constexpr const char* kValueMeta = "LuaCS.Value";
inline constexpr const char* kDisabledRegistryKey = "LuaCS.Disabled";
inline constexpr const char* kDisableReasonRegistryKey = "LuaCS.DisableReason";

inline std::string trim(std::string_view input) {
    auto begin = input.begin();
    auto end = input.end();
    while (begin != end && std::isspace(static_cast<unsigned char>(*begin))) ++begin;
    while (end != begin && std::isspace(static_cast<unsigned char>(*(end - 1)))) --end;
    return {begin, end};
}

inline void push_string_field(lua_State* state, const char* key, std::string_view value) {
    lua_pushlstring(state, value.data(), value.size());
    lua_setfield(state, -2, key);
}

inline void push_bool_field(lua_State* state, const char* key, bool value) {
    lua_pushboolean(state, value);
    lua_setfield(state, -2, key);
}

inline void push_integer_field(lua_State* state, const char* key, lua_Integer value) {
    lua_pushinteger(state, value);
    lua_setfield(state, -2, key);
}

inline bool vm_disabled(lua_State* state) {
    if (!state) return true;
    lua_getfield(state, LUA_REGISTRYINDEX, kDisabledRegistryKey);
    const bool disabled = lua_toboolean(state, -1) != 0;
    lua_pop(state, 1);
    return disabled;
}

inline void disable_vm(lua_State* state, std::string_view reason) {
    if (!state) return;
    lua_pushboolean(state, 1);
    lua_setfield(state, LUA_REGISTRYINDEX, kDisabledRegistryKey);
    lua_pushlstring(state, reason.data(), reason.size());
    lua_setfield(state, LUA_REGISTRYINDEX, kDisableReasonRegistryKey);
}

inline std::string vm_disable_reason(lua_State* state) {
    if (!state) return {};
    lua_getfield(state, LUA_REGISTRYINDEX, kDisableReasonRegistryKey);
    std::size_t size{};
    const char* text = lua_tolstring(state, -1, &size);
    std::string result;
    if (text && size != 0) result.assign(text, size);
    lua_pop(state, 1);
    return result;
}

inline std::string steam3_from_steam64(std::uint64_t steam64) {
    constexpr std::uint64_t kBase = 76561197960265728ULL;
    if (steam64 < kBase) return {};
    return "[U:1:" + std::to_string(steam64 - kBase) + "]";
}

inline std::string steam2_from_steam64(std::uint64_t steam64) {
    constexpr std::uint64_t kBase = 76561197960265728ULL;
    if (steam64 < kBase) return {};
    const auto account = steam64 - kBase;
    return "STEAM_1:" + std::to_string(account & 1ULL) + ":" + std::to_string(account / 2ULL);
}

inline std::filesystem::path module_path(const std::filesystem::path& bin_dir,
                                         std::string_view module_name) {
    if (!module_name.starts_with("cs2.")) return {};
    auto short_name = std::string(module_name.substr(4));
    if (short_name.empty() ||
        short_name.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789_") != std::string::npos) {
        return {};
    }
    return bin_dir / (short_name + ".dll");
}

} // namespace luacs::detail
