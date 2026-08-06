#include "runtime.h"
#include "runtime_helpers.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <sstream>

namespace luacs {
using namespace detail;

int Runtime::lua_print(lua_State* state) {
    lua_getfield(state, LUA_REGISTRYINDEX, kVmRegistryKey);
    auto* vm = static_cast<ScriptVm*>(lua_touserdata(state, -1));
    lua_pop(state, 1);
    if (!vm || !vm->runtime) return 0;
    std::ostringstream line;
    const int count = lua_gettop(state);
    for (int index = 1; index <= count; ++index) {
        if (index > 1) line << '\t';
        size_t length = 0;
        const char* value = luaL_tolstring(state, index, &length);
        if (value) line.write(value, static_cast<std::streamsize>(length));
        lua_pop(state, 1);
    }
    vm->runtime->log(*vm, line.str());
    return 0;
}

int Runtime::lua_vector(lua_State* state) {
    const double x = luaL_optnumber(state, 1, 0.0);
    const double y = luaL_optnumber(state, 2, 0.0);
    const double z = luaL_optnumber(state, 3, 0.0);
    lua_createtable(state, 0, 4);
    lua_pushnumber(state, x);
    lua_setfield(state, -2, "x");
    lua_pushnumber(state, y);
    lua_setfield(state, -2, "y");
    lua_pushnumber(state, z);
    lua_setfield(state, -2, "z");
    lua_pushliteral(state, "Vector");
    lua_setfield(state, -2, "__type");
    luaL_getmetatable(state, kValueMeta);
    lua_setmetatable(state, -2);
    return 1;
}

int Runtime::lua_color(lua_State* state) {
    const lua_Integer r = luaL_checkinteger(state, 1);
    const lua_Integer g = luaL_checkinteger(state, 2);
    const lua_Integer b = luaL_checkinteger(state, 3);
    const lua_Integer a = luaL_optinteger(state, 4, 255);
    auto valid = [](lua_Integer value) {
        return value >= 0 && value <= 255;
    };
    if (!valid(r) || !valid(g) || !valid(b) || !valid(a)) {
        return luaL_error(state,
                          "Color channels must be between 0 and 255");
    }
    lua_createtable(state, 0, 5);
    lua_pushinteger(state, r);
    lua_setfield(state, -2, "r");
    lua_pushinteger(state, g);
    lua_setfield(state, -2, "g");
    lua_pushinteger(state, b);
    lua_setfield(state, -2, "b");
    lua_pushinteger(state, a);
    lua_setfield(state, -2, "a");
    lua_pushliteral(state, "Color");
    lua_setfield(state, -2, "__type");
    luaL_getmetatable(state, kValueMeta);
    lua_setmetatable(state, -2);
    return 1;
}

int Runtime::lua_value_tostring(lua_State* state) {
    lua_getfield(state, 1, "__type");
    const char* type = lua_tostring(state, -1);
    lua_pop(state, 1);
    std::ostringstream result;
    if (type && std::string_view(type) == "Vector") {
        lua_getfield(state, 1, "x");
        const double x = lua_tonumber(state, -1);
        lua_pop(state, 1);
        lua_getfield(state, 1, "y");
        const double y = lua_tonumber(state, -1);
        lua_pop(state, 1);
        lua_getfield(state, 1, "z");
        const double z = lua_tonumber(state, -1);
        lua_pop(state, 1);
        result << "Vector(" << x << ", " << y << ", " << z << ")";
    } else {
        lua_getfield(state, 1, "r");
        const auto r = lua_tointeger(state, -1);
        lua_pop(state, 1);
        lua_getfield(state, 1, "g");
        const auto g = lua_tointeger(state, -1);
        lua_pop(state, 1);
        lua_getfield(state, 1, "b");
        const auto b = lua_tointeger(state, -1);
        lua_pop(state, 1);
        lua_getfield(state, 1, "a");
        const auto a = lua_tointeger(state, -1);
        lua_pop(state, 1);
        result << "Color(" << r << ", " << g << ", " << b << ", " << a
               << ")";
    }
    lua_pushstring(state, result.str().c_str());
    return 1;
}

int Runtime::lua_module_searcher(lua_State* state) {
    const char* name = luaL_checkstring(state, 1);
    lua_getfield(state, LUA_REGISTRYINDEX, kRuntimeRegistryKey);
    auto* runtime = static_cast<Runtime*>(lua_touserdata(state, -1));
    lua_pop(state, 1);
    if (!runtime) {
        lua_pushliteral(state, "\n\tLuaCS runtime is unavailable");
        return 1;
    }
    if (std::string_view(name) == "cs2") {
        lua_pushcfunction(state, &Runtime::lua_cs2_root_loader);
        return 1;
    }
    const auto path = module_path(runtime->bin_dir_, name);
    if (path.empty() || !std::filesystem::exists(path)) {
        lua_pushfstring(state, "\n\tno LuaCS module '%s' in %s", name,
                        runtime->bin_dir_.string().c_str());
        return 1;
    }
    lua_pushstring(state, path.string().c_str());
    lua_pushstring(state, name);
    lua_pushcclosure(state, &Runtime::lua_module_loader, 2);
    return 1;
}

int Runtime::lua_module_loader(lua_State* state) {
    const char* path = lua_tostring(state, lua_upvalueindex(1));
    const char* module_name = lua_tostring(state, lua_upvalueindex(2));
    lua_getfield(state, LUA_REGISTRYINDEX, kRuntimeRegistryKey);
    auto* runtime = static_cast<Runtime*>(lua_touserdata(state, -1));
    lua_pop(state, 1);
    lua_getfield(state, LUA_REGISTRYINDEX, kVmRegistryKey);
    auto* vm = static_cast<ScriptVm*>(lua_touserdata(state, -1));
    lua_pop(state, 1);
    if (!runtime || !vm) {
        return luaL_error(state, "LuaCS runtime state is unavailable");
    }

    HMODULE handle = nullptr;
    {
        const auto wide_path = std::filesystem::path(path).wstring();
        handle = LoadLibraryW(wide_path.c_str());
    }
    if (!handle) {
        return luaL_error(state,
                          "could not load native module %s (Windows error %lu)",
                          path, GetLastError());
    }
    auto open = reinterpret_cast<OpenModuleFn>(
        GetProcAddress(handle, "LuaCS_OpenModule"));
    if (!open) {
        FreeLibrary(handle);
        return luaL_error(state, "%s does not export LuaCS_OpenModule", path);
    }

    lua_pushlightuserdata(state, reinterpret_cast<void*>(open));
    lua_pushlightuserdata(state, &runtime->services_);
    lua_pushcclosure(state, &Runtime::lua_native_open_trampoline, 2);
    const int status = lua_pcall(state, 0, 1, 0);
    if (status != LUA_OK) {
        FreeLibrary(handle);
        return lua_error(state);
    }
    if (!lua_istable(state, -1)) {
        lua_pop(state, 1);
        FreeLibrary(handle);
        return luaL_error(state, "module %s returned an invalid module table",
                          module_name);
    }
    vm->module_handles.push_back(handle);
    return 1;
}

int Runtime::lua_native_open_trampoline(lua_State* state) {
    auto open = reinterpret_cast<OpenModuleFn>(
        lua_touserdata(state, lua_upvalueindex(1)));
    auto* services = static_cast<const Services*>(
        lua_touserdata(state, lua_upvalueindex(2)));
    if (!open || !services) {
        return luaL_error(state,
                          "native module loader state is invalid");
    }
    const int results = open(state, services);
    if (results != 1) {
        return luaL_error(state,
                          "native module must return exactly one value");
    }
    return 1;
}

int Runtime::lua_cs2_root_loader(lua_State* state) {
    lua_newtable(state);
    lua_newtable(state);
    lua_pushcfunction(state, &Runtime::lua_cs2_root_index);
    lua_setfield(state, -2, "__index");
    lua_setmetatable(state, -2);
    return 1;
}

int Runtime::lua_cs2_root_index(lua_State* state) {
    const char* key = luaL_checkstring(state, 2);
    std::string module = "cs2.";
    module += key;
    lua_getglobal(state, "require");
    lua_pushstring(state, module.c_str());
    lua_call(state, 1, 1);
    lua_pushvalue(state, 2);
    lua_pushvalue(state, -2);
    lua_rawset(state, 1);
    return 1;
}

int Runtime::lua_traceback(lua_State* state) {
    const char* message = lua_tostring(state, 1);
    if (message) {
        luaL_traceback(state, state, message, 1);
    } else {
        lua_pushliteral(state, "(non-string Lua error)");
    }
    return 1;
}

bool Runtime::install_core(ScriptVm& vm) {
    lua_State* state = vm.state;
    lua_pushlightuserdata(state, this);
    lua_setfield(state, LUA_REGISTRYINDEX, kRuntimeRegistryKey);
    lua_pushlightuserdata(state, &vm);
    lua_setfield(state, LUA_REGISTRYINDEX, kVmRegistryKey);

    lua_pushcfunction(state, &Runtime::lua_print);
    lua_setglobal(state, "print");
    lua_pushcfunction(state, &Runtime::lua_vector);
    lua_setglobal(state, "Vector");
    lua_pushcfunction(state, &Runtime::lua_color);
    lua_setglobal(state, "Color");

    if (luaL_newmetatable(state, kValueMeta)) {
        lua_pushcfunction(state, &Runtime::lua_value_tostring);
        lua_setfield(state, -2, "__tostring");
    }
    lua_pop(state, 1);

    lua_getglobal(state, "package");
    lua_getfield(state, -1, "searchers");
    const lua_Integer length =
        static_cast<lua_Integer>(lua_rawlen(state, -1));
    for (lua_Integer index = length + 1; index > 2; --index) {
        lua_geti(state, -1, index - 1);
        lua_seti(state, -2, index);
    }
    lua_pushcfunction(state, &Runtime::lua_module_searcher);
    lua_seti(state, -2, 2);
    lua_pop(state, 2);

    const auto require_global = [&](const char* module_name,
                                    const char* global_name) {
        lua_getglobal(state, "require");
        lua_pushstring(state, module_name);
        if (!protected_call(vm, 1, 1,
                            std::string("loading core module '") +
                                module_name + "'")) {
            return false;
        }
        lua_setglobal(state, global_name);
        return true;
    };

    return require_global("cs2", "cs2") &&
           require_global("cs2.players", "players") &&
           require_global("cs2.events", "events") &&
           require_global("cs2.timers", "timers");
}

bool Runtime::protected_call(ScriptVm& vm, int argument_count,
                             int result_count, std::string_view context) {
    const int function_index = lua_gettop(vm.state) - argument_count;
    lua_pushcfunction(vm.state, &Runtime::lua_traceback);
    lua_insert(vm.state, function_index);
    const int status = lua_pcall(vm.state, argument_count, result_count,
                                 function_index);
    lua_remove(vm.state, function_index);
    if (status == LUA_OK) return true;
    const char* message = lua_tostring(vm.state, -1);
    log(vm, std::string(context) + " failed: " +
                (message ? message : "unknown Lua error"));
    lua_pop(vm.state, 1);
    return false;
}

void Runtime::push_player(lua_State* state,
                          const PlayerSnapshot& player) const {
    lua_createtable(state, 0, 10);
    push_integer_field(state, "slot", player.slot);
    push_string_field(state, "name", player.name);
    lua_pushinteger(state, static_cast<lua_Integer>(player.steam64));
    lua_setfield(state, -2, "steam64");
    push_string_field(state, "steamid",
                      player.steam_id.empty()
                          ? steam2_from_steam64(player.steam64)
                          : player.steam_id);
    push_string_field(state, "steam3", steam3_from_steam64(player.steam64));
    push_bool_field(state, "fake", player.fake);
    push_bool_field(state, "connected", player.connected);
    push_bool_field(state, "active", player.active);

    luaL_getmetatable(state, "LuaCS.Player");
    if (lua_istable(state, -1)) {
        lua_setmetatable(state, -2);
    } else {
        lua_pop(state, 1);
    }
}

std::string Runtime::normalize_name(std::string_view name) {
    std::string result;
    result.reserve(name.size());
    for (char value : name) {
        if (std::isalnum(static_cast<unsigned char>(value)) || value == '_') {
            result.push_back(static_cast<char>(
                std::tolower(static_cast<unsigned char>(value))));
        }
    }
    return result;
}

std::pair<std::string, std::string> Runtime::parse_command(
    std::string_view command_line) {
    std::string raw = trim(command_line);
    if (raw.empty()) return {};
    const auto first_space = raw.find_first_of(" \t");
    std::string first = normalize_name(raw.substr(0, first_space));
    std::string rest = first_space == std::string::npos
                           ? std::string{}
                           : trim(std::string_view(raw).substr(first_space + 1));
    if (first == "say" || first == "say_team") {
        if (rest.size() >= 2 && rest.front() == '"' && rest.back() == '"') {
            rest = rest.substr(1, rest.size() - 2);
        }
        if (rest.empty() || (rest.front() != '!' && rest.front() != '/')) {
            return {};
        }
        rest.erase(rest.begin());
        const auto split = rest.find_first_of(" \t");
        return {normalize_name(rest.substr(0, split)),
                split == std::string::npos
                    ? std::string{}
                    : trim(std::string_view(rest).substr(split + 1))};
    }
    return {first, rest};
}

} // namespace luacs
