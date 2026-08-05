#include "runtime.h"
#include "runtime_helpers.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <fstream>
#include <sstream>

namespace luacs {
using namespace detail;

void Runtime::level_init(std::string_view map_name) {
    emit("map_start", [&](lua_State* state) {
        lua_newtable(state);
        push_string_field(state, "map", map_name);
    });
}

void Runtime::level_shutdown() {
    emit("map_end", [](lua_State* state) { lua_newtable(state); });
}

void Runtime::player_connected(int slot, std::string_view name, std::uint64_t steam64,
                               std::string_view steam_id, bool fake) {
    auto& player = players_[slot];
    player.slot = slot;
    player.name = name;
    player.steam64 = steam64;
    player.steam_id = steam_id.empty() ? steam2_from_steam64(steam64) : std::string(steam_id);
    player.fake = fake;
    player.connected = true;
    emit_player("player_connect", player);
}

void Runtime::player_active(int slot, std::string_view name, std::uint64_t steam64) {
    auto& player = players_[slot];
    player.slot = slot;
    if (!name.empty()) player.name = name;
    if (steam64) player.steam64 = steam64;
    if (player.steam_id.empty()) player.steam_id = steam2_from_steam64(player.steam64);
    player.connected = true;
    player.active = true;
    emit_player("player_activate", player);
}

void Runtime::player_put_in_server(int slot, std::string_view name, std::uint64_t steam64) {
    auto& player = players_[slot];
    player.slot = slot;
    if (!name.empty()) player.name = name;
    if (steam64) player.steam64 = steam64;
    if (player.steam_id.empty()) player.steam_id = steam2_from_steam64(player.steam64);
    player.connected = true;
    emit_player("player_put_in_server", player);
}

void Runtime::player_disconnected(int slot, std::string_view name, std::uint64_t steam64,
                                  std::string_view steam_id) {
    PlayerSnapshot player;
    if (const auto found = players_.find(slot); found != players_.end()) player = found->second;
    player.slot = slot;
    if (!name.empty()) player.name = name;
    if (steam64) player.steam64 = steam64;
    if (!steam_id.empty()) player.steam_id = steam_id;
    player.connected = false;
    player.active = false;
    emit_player("player_disconnect", player);
    players_.erase(slot);
}

void Runtime::client_command(int slot, std::string_view command_line) {
    const PlayerSnapshot* player = nullptr;
    if (const auto found = players_.find(slot); found != players_.end()) player = &found->second;

    emit("client_command", [&](lua_State* state) {
        lua_newtable(state);
        if (player) {
            push_player(state, *player);
            lua_setfield(state, -2, "player");
        }
        push_string_field(state, "raw", command_line);
    });

    auto [name, arguments] = parse_command(command_line);
    if (!name.empty()) dispatch_command(player, name, arguments, command_line);
}

double Runtime::now() const {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - started_).count();
}

void Runtime::emit(std::string_view event_name,
                   const std::function<void(lua_State*)>& push_event_table) {
    const std::string key = normalize_name(event_name);
    for (auto& vm_ptr : scripts_) {
        auto& vm = *vm_ptr;
        const auto found = vm.events.find(key);
        if (found == vm.events.end()) continue;
        auto callbacks = found->second;
        for (const auto& callback : callbacks) {
            lua_rawgeti(vm.state, LUA_REGISTRYINDEX, callback.reference);
            push_event_table(vm.state);
            if (callback.mode == EventCallbackMode::PlayerOnly) {
                lua_getfield(vm.state, -1, "player");
                lua_remove(vm.state, -2);
            }
            protected_call(vm, 1, 0, std::string("event '") + key + "'");
        }
    }
}

void Runtime::emit_player(std::string_view event_name, const PlayerSnapshot& player) {
    emit(event_name, [&](lua_State* state) {
        lua_newtable(state);
        push_player(state, player);
        lua_setfield(state, -2, "player");
    });
}

void Runtime::dispatch_command(const PlayerSnapshot* player, std::string_view name,
                               std::string_view arguments, std::string_view raw) {
    const std::string key = normalize_name(name);
    for (auto& vm_ptr : scripts_) {
        auto& vm = *vm_ptr;
        const auto found = vm.commands.find(key);
        if (found == vm.commands.end()) continue;
        auto callbacks = found->second;
        for (int reference : callbacks) {
            lua_rawgeti(vm.state, LUA_REGISTRYINDEX, reference);
            if (player) push_player(vm.state, *player); else lua_pushnil(vm.state);
            lua_pushlstring(vm.state, arguments.data(), arguments.size());
            lua_pushlstring(vm.state, raw.data(), raw.size());
            protected_call(vm, 3, 0, std::string("command '") + key + "'");
        }
    }
}

void Runtime::log(ScriptVm& vm, std::string_view text) {
    std::ofstream output(vm.log_path, std::ios::app);
    if (output) output << text << '\n';
    log_runtime("[LuaCS:" + vm.name + "] " + std::string(text));
}

void Runtime::log_runtime(std::string_view text) {
    if (console_writer_) console_writer_(text);
}

std::filesystem::path Runtime::create_log_path() const {
    const auto now_time = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm local{};
    localtime_s(&local, &now_time);
    for (std::uint32_t index = 1; index < 1000000; ++index) {
        std::ostringstream name;
        name << local.tm_mday << (local.tm_mon + 1) << (local.tm_year + 1900)
             << local.tm_hour << local.tm_min << local.tm_sec << index << ".log";
        auto path = logs_dir_ / name.str();
        if (!std::filesystem::exists(path)) return path;
    }
    return logs_dir_ / (std::to_string(now_time) + "1000000.log");
}

Runtime::ScriptVm* Runtime::find_vm(lua_State* state) {
    const auto found = state_map_.find(state);
    return found == state_map_.end() ? nullptr : found->second;
}

const Runtime::ScriptVm* Runtime::find_vm(lua_State* state) const {
    const auto found = state_map_.find(state);
    return found == state_map_.end() ? nullptr : found->second;
}

} // namespace luacs
