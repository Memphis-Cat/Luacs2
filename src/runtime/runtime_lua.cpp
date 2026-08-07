#include "runtime.h"
#include "runtime_helpers.h"

#include "luacs/lua_checked.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <exception>
#include <filesystem>
#include <limits>
#include <sstream>
#include <string>
#include <system_error>

namespace luacs {
using namespace detail;

namespace {

std::string path_text(const std::filesystem::path& path) {
    const auto utf8 = path.u8string();
    return {reinterpret_cast<const char*>(utf8.data()), utf8.size()};
}

std::filesystem::path path_from_utf8(const char* value, std::size_t size) {
    if (!value) return {};
    return std::filesystem::u8path(value, value + size);
}

} // namespace

int Runtime::lua_print(lua_State* state) {
    lua_getfield(state, LUA_REGISTRYINDEX, kVmRegistryKey);
    auto* vm = static_cast<ScriptVm*>(lua_touserdata(state, -1));
    lua_pop(state, 1);
    if (!vm || !vm->runtime) return 0;

    try {
        std::string line;
        const int count = lua_gettop(state);
        for (int index = 1; index <= count; ++index) {
            if (index > 1) line.push_back('\t');
            std::size_t length = 0;
            const char* value = luaL_tolstring(state, index, &length);
            if (value && length != 0) line.append(value, length);
            lua_pop(state, 1);
        }
        vm->runtime->log(*vm, line);
    } catch (const std::exception& exception) {
        return luaL_error(state, "print failed: %s", exception.what());
    } catch (...) {
        return luaL_error(state, "print failed with an unknown C++ exception");
    }
    return 0;
}

int Runtime::lua_vector(lua_State* state) {
    const double x = lua_isnoneornil(state, 1)
                         ? 0.0
                         : lua_checked::finite_number(
                               state, 1, "Vector x component must be finite");
    const double y = lua_isnoneornil(state, 2)
                         ? 0.0
                         : lua_checked::finite_number(
                               state, 2, "Vector y component must be finite");
    const double z = lua_isnoneornil(state, 3)
                         ? 0.0
                         : lua_checked::finite_number(
                               state, 3, "Vector z component must be finite");
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
    const int r = lua_checked::checked_int(
        state, 1, 0, 255, "Color red channel must be between 0 and 255");
    const int g = lua_checked::checked_int(
        state, 2, 0, 255, "Color green channel must be between 0 and 255");
    const int b = lua_checked::checked_int(
        state, 3, 0, 255, "Color blue channel must be between 0 and 255");
    const int a = lua_isnoneornil(state, 4)
                      ? 255
                      : lua_checked::checked_int(
                            state, 4, 0, 255,
                            "Color alpha channel must be between 0 and 255");
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
    luaL_checktype(state, 1, LUA_TTABLE);
    lua_getfield(state, 1, "__type");
    const char* type = lua_tostring(state, -1);
    const std::string stable_type = type ? type : "";
    lua_pop(state, 1);

    try {
        std::ostringstream result;
        if (stable_type == "Vector") {
            lua_getfield(state, 1, "x");
            const double x = lua_checked::finite_number(
                state, -1, "Vector x component must be finite");
            lua_pop(state, 1);
            lua_getfield(state, 1, "y");
            const double y = lua_checked::finite_number(
                state, -1, "Vector y component must be finite");
            lua_pop(state, 1);
            lua_getfield(state, 1, "z");
            const double z = lua_checked::finite_number(
                state, -1, "Vector z component must be finite");
            lua_pop(state, 1);
            result << "Vector(" << x << ", " << y << ", " << z << ")";
        } else if (stable_type == "Color") {
            lua_getfield(state, 1, "r");
            const int r = lua_checked::checked_int(
                state, -1, 0, 255, "Color red channel is invalid");
            lua_pop(state, 1);
            lua_getfield(state, 1, "g");
            const int g = lua_checked::checked_int(
                state, -1, 0, 255, "Color green channel is invalid");
            lua_pop(state, 1);
            lua_getfield(state, 1, "b");
            const int b = lua_checked::checked_int(
                state, -1, 0, 255, "Color blue channel is invalid");
            lua_pop(state, 1);
            lua_getfield(state, 1, "a");
            const int a = lua_checked::checked_int(
                state, -1, 0, 255, "Color alpha channel is invalid");
            lua_pop(state, 1);
            result << "Color(" << r << ", " << g << ", " << b << ", " << a
                   << ")";
        } else {
            return luaL_error(state, "unknown LuaCS value type '%s'",
                              stable_type.c_str());
        }
        const std::string text = result.str();
        lua_pushlstring(state, text.data(), text.size());
        return 1;
    } catch (const std::exception& exception) {
        return luaL_error(state, "value formatting failed: %s",
                          exception.what());
    } catch (...) {
        return luaL_error(state,
                          "value formatting failed with an unknown C++ exception");
    }
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
    std::error_code exists_error;
    const bool exists = !path.empty() &&
                        std::filesystem::exists(path, exists_error);
    if (exists_error || !exists) {
        const std::string directory = path_text(runtime->bin_dir_);
        lua_pushfstring(state, "\n\tno LuaCS module '%s' in %s", name,
                        directory.c_str());
        return 1;
    }

    const std::string utf8_path = path_text(path);
    lua_pushlstring(state, utf8_path.data(), utf8_path.size());
    lua_pushstring(state, name);
    lua_pushcclosure(state, &Runtime::lua_module_loader, 2);
    return 1;
}

int Runtime::lua_module_loader(lua_State* state) {
    std::size_t path_size = 0;
    const char* path =
        lua_tolstring(state, lua_upvalueindex(1), &path_size);
    const char* module_name = lua_tostring(state, lua_upvalueindex(2));
    if (!path || path_size == 0 || !module_name || !module_name[0]) {
        return luaL_error(state, "LuaCS native module loader metadata is invalid");
    }

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
    std::filesystem::path native_path;
    try {
        native_path = path_from_utf8(path, path_size);
        handle = LoadLibraryW(native_path.c_str());
    } catch (const std::exception& exception) {
        return luaL_error(state, "could not decode native module path: %s",
                          exception.what());
    }
    if (!handle) {
        const DWORD code = GetLastError();
        return luaL_error(state,
                          "could not load native module %s (Windows error %lu)",
                          path, static_cast<unsigned long>(code));
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

    try {
        vm->module_handles.push_back(handle);
    } catch (const std::exception& exception) {
        lua_pop(state, 1);
        FreeLibrary(handle);
        return luaL_error(state, "could not retain module %s: %s", module_name,
                          exception.what());
    } catch (...) {
        lua_pop(state, 1);
        FreeLibrary(handle);
        return luaL_error(state,
                          "could not retain module %s due to an unknown C++ exception",
                          module_name);
    }
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
    try {
        const int results = open(state, services);
        if (results != 1) {
            return luaL_error(state,
                              "native module must return exactly one value");
        }
        return 1;
    } catch (const std::exception& exception) {
        return luaL_error(state, "native module threw C++ exception: %s",
                          exception.what());
    } catch (...) {
        return luaL_error(state, "native module threw an unknown C++ exception");
    }
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
    if (!lua_istable(state, -1)) {
        lua_pop(state, 1);
        log(vm, "[ERROR] Lua package table is unavailable.");
        return false;
    }
    lua_getfield(state, -1, "searchers");
    if (!lua_istable(state, -1)) {
        lua_pop(state, 2);
        log(vm, "[ERROR] Lua package.searchers table is unavailable.");
        return false;
    }
    const lua_Integer length =
        static_cast<lua_Integer>(lua_rawlen(state, -1));
    if (length >= std::numeric_limits<lua_Integer>::max()) {
        lua_pop(state, 2);
        log(vm, "[ERROR] Lua package.searchers table is too large.");
        return false;
    }
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
        if (!lua_isfunction(state, -1)) {
            lua_pop(state, 1);
            log(vm, "[ERROR] Lua require function is unavailable.");
            return false;
        }
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
    const int top = lua_gettop(vm.state);
    if (argument_count < 0 || top < argument_count + 1) {
        log(vm, std::string(context) +
                    " failed: invalid protected-call stack state");
        return false;
    }
    const int function_index = top - argument_count;
    if (!lua_isfunction(vm.state, function_index)) {
        log(vm, std::string(context) +
                    " failed: protected-call target is not a function");
        return false;
    }
    lua_pushcfunction(vm.state, &Runtime::lua_traceback);
    lua_insert(vm.state, function_index);
    const int status = lua_pcall(vm.state, argument_count, result_count,
                                 function_index);
    lua_remove(vm.state, function_index);
    if (status == LUA_OK) return true;

    const char* raw_message = lua_tostring(vm.state, -1);
    const std::string message =
        raw_message ? raw_message : "unknown Lua error";
    log(vm, std::string(context) + " failed: " + message);
    lua_pop(vm.state, 1);

    bool loaded_plugin = false;
    for (const auto& loaded : scripts_) {
        if (loaded.get() == &vm) {
            loaded_plugin = true;
            break;
        }
    }
    if (!loaded_plugin || vm_disabled(vm.state)) return false;

    disable_vm(vm.state, std::string(context) + ": " + message);

    for (auto& entry : vm.events) {
        for (const auto& callback : entry.second) {
            luaL_unref(vm.state, LUA_REGISTRYINDEX, callback.reference);
        }
    }
    vm.events.clear();

    for (auto& entry : vm.commands) {
        for (int reference : entry.second) {
            luaL_unref(vm.state, LUA_REGISTRYINDEX, reference);
        }
    }
    vm.commands.clear();

    for (const auto& timer : vm.timers) {
        luaL_unref(vm.state, LUA_REGISTRYINDEX, timer.reference);
    }
    vm.timers.clear();

    log(vm, "[ERROR] Plugin disabled after an uncaught Lua callback error. "
            "Refresh or reload the plugin after fixing it; other plugins remain active.");
    return false;
}

void Runtime::push_player(lua_State* state,
                          const PlayerSnapshot& player) const {
    lua_createtable(state, 0, 10);
    push_integer_field(state, "slot", player.slot);
    push_string_field(state, "name", player.name);
    lua_checked::push_u64_exact(state, player.steam64);
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
        const std::string command = normalize_name(rest.substr(0, split));
        if (command.empty()) return {};
        return {command,
                split == std::string::npos
                    ? std::string{}
                    : trim(std::string_view(rest).substr(split + 1))};
    }
    if (first.empty()) return {};
    return {first, rest};
}

} // namespace luacs
