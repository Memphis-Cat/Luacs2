#include "runtime.h"
#include "runtime_helpers.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <ctime>
#include <fstream>
#include <sstream>

namespace luacs {
using namespace detail;

namespace {

inline constexpr const char* kEventMeta = "LuaCS.Event";

struct ParsedLogMessage {
    std::string_view level{"INFO"};
    std::string_view message;
};

ParsedLogMessage parse_level(std::string_view text) {
    if (text.starts_with("[ERROR]")) {
        text.remove_prefix(7);
        while (!text.empty() && text.front() == ' ') text.remove_prefix(1);
        return {"ERROR", text};
    }
    if (text.starts_with("[WARN]")) {
        text.remove_prefix(6);
        while (!text.empty() && text.front() == ' ') text.remove_prefix(1);
        return {"WARN", text};
    }
    if (text.starts_with("[DEBUG]")) {
        text.remove_prefix(7);
        while (!text.empty() && text.front() == ' ') text.remove_prefix(1);
        return {"DEBUG", text};
    }
    return {"INFO", text};
}

std::string current_clock() {
    const auto time_value =
        std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm local{};
    localtime_s(&local, &time_value);

    char buffer[16]{};
    std::strftime(buffer, sizeof(buffer), "%H:%M:%S", &local);
    return buffer;
}

std::string format_log_line(std::string_view level, std::string_view source,
                            std::string_view message) {
    std::string line;
    line.reserve(32 + source.size() + message.size());
    line += current_clock();
    line += " [";
    line += level;
    line += "] (";
    line += source;
    line += ") ";
    line += message;
    return line;
}

void append_line(const std::filesystem::path& path, std::string_view line) {
    if (path.empty()) return;
    std::ofstream output(path, std::ios::app);
    if (output) output << line << '\n';
}

} // namespace

void Runtime::level_init(std::string_view map_name) {
    log_runtime("Map started: " + std::string(map_name));
    emit("map_start", [&](lua_State* state) {
        lua_newtable(state);
        push_string_field(state, "map", map_name);
    });
}

void Runtime::level_shutdown() {
    log_runtime("Map is shutting down.");
    emit("map_end", [](lua_State* state) { lua_newtable(state); });
}

void Runtime::player_connected(int slot, std::string_view name,
                               std::uint64_t steam64,
                               std::string_view steam_id, bool fake) {
    auto& player = players_[slot];
    player.slot = slot;
    player.name = name;
    player.steam64 = steam64;
    player.steam_id =
        steam_id.empty() ? steam2_from_steam64(steam64) : std::string(steam_id);
    player.fake = fake;
    player.connected = true;
    log_runtime("Player connected: " + player.name + " (slot " +
                std::to_string(player.slot) + ")");
    emit_player("player_connect", player);
}

void Runtime::player_active(int slot, std::string_view name,
                            std::uint64_t steam64) {
    auto& player = players_[slot];
    player.slot = slot;
    if (!name.empty()) player.name = name;
    if (steam64) player.steam64 = steam64;
    if (player.steam_id.empty()) {
        player.steam_id = steam2_from_steam64(player.steam64);
    }
    player.connected = true;
    player.active = true;
    emit_player("player_activate", player);
}

void Runtime::player_put_in_server(int slot, std::string_view name,
                                   std::uint64_t steam64) {
    auto& player = players_[slot];
    player.slot = slot;
    if (!name.empty()) player.name = name;
    if (steam64) player.steam64 = steam64;
    if (player.steam_id.empty()) {
        player.steam_id = steam2_from_steam64(player.steam64);
    }
    player.connected = true;
    emit_player("player_put_in_server", player);
}

void Runtime::player_disconnected(int slot, std::string_view name,
                                  std::uint64_t steam64,
                                  std::string_view steam_id) {
    PlayerSnapshot player;
    if (const auto found = players_.find(slot); found != players_.end()) {
        player = found->second;
    }
    player.slot = slot;
    if (!name.empty()) player.name = name;
    if (steam64) player.steam64 = steam64;
    if (!steam_id.empty()) player.steam_id = steam_id;
    player.connected = false;
    player.active = false;
    emit_player("player_disconnect", player);
    log_runtime("Player disconnected: " + player.name + " (slot " +
                std::to_string(player.slot) + ")");
    players_.erase(slot);
}

void Runtime::client_command(int slot, std::string_view command_line) {
    const PlayerSnapshot* player = player_snapshot(slot);

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

void Runtime::dispatch_game_event(std::uint64_t token, std::string_view name,
                                  int id, bool reliable, bool local, bool post,
                                  bool dont_broadcast) {
    const std::string key = normalize_name(name);
    if (key.empty() || token == 0) return;

    for (auto& vm_ptr : scripts_) {
        auto& vm = *vm_ptr;
        const auto found = vm.events.find(key);
        if (found == vm.events.end()) continue;

        const auto callbacks = found->second;
        for (const auto& callback : callbacks) {
            if (callback.post != post) continue;

            lua_rawgeti(vm.state, LUA_REGISTRYINDEX, callback.reference);
            if (callback.mode == EventCallbackMode::PlayerOnly) {
                int slot = -1;
                if (host_operations_.event_get_player_slot &&
                    host_operations_.event_get_player_slot(token, "userid",
                                                           slot)) {
                    if (const auto* player = player_snapshot(slot)) {
                        push_player(vm.state, *player);
                    } else {
                        lua_pushnil(vm.state);
                    }
                } else {
                    lua_pushnil(vm.state);
                }
            } else {
                lua_createtable(vm.state, 0, 9);
                lua_pushinteger(vm.state,
                                static_cast<lua_Integer>(token));
                lua_setfield(vm.state, -2, "__token");
                push_string_field(vm.state, "name", name);
                push_integer_field(vm.state, "id", id);
                push_bool_field(vm.state, "reliable", reliable);
                push_bool_field(vm.state, "local", local);
                push_bool_field(vm.state, "post", post);
                push_bool_field(vm.state, "dont_broadcast", dont_broadcast);

                luaL_getmetatable(vm.state, kEventMeta);
                if (lua_istable(vm.state, -1)) {
                    lua_setmetatable(vm.state, -2);
                } else {
                    lua_pop(vm.state, 1);
                }
            }

            protected_call(vm, 1, 0,
                           std::string(post ? "post game event '"
                                            : "pre game event '") +
                               key + "'");
        }
    }
}

double Runtime::now() const {
    return std::chrono::duration<double>(
               std::chrono::steady_clock::now() - started_)
        .count();
}

void Runtime::emit(
    std::string_view event_name,
    const std::function<void(lua_State*)>& push_event_table) {
    const std::string key = normalize_name(event_name);
    for (auto& vm_ptr : scripts_) {
        auto& vm = *vm_ptr;
        const auto found = vm.events.find(key);
        if (found == vm.events.end()) continue;
        const auto callbacks = found->second;
        for (const auto& callback : callbacks) {
            if (callback.post) continue;
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

void Runtime::emit_player(std::string_view event_name,
                          const PlayerSnapshot& player) {
    emit(event_name, [&](lua_State* state) {
        lua_newtable(state);
        push_player(state, player);
        lua_setfield(state, -2, "player");
    });
}

void Runtime::dispatch_command(const PlayerSnapshot* player,
                               std::string_view name,
                               std::string_view arguments,
                               std::string_view raw) {
    const std::string key = normalize_name(name);
    for (auto& vm_ptr : scripts_) {
        auto& vm = *vm_ptr;
        const auto found = vm.commands.find(key);
        if (found == vm.commands.end()) continue;
        const auto callbacks = found->second;
        for (int reference : callbacks) {
            lua_rawgeti(vm.state, LUA_REGISTRYINDEX, reference);
            if (player) {
                push_player(vm.state, *player);
            } else {
                lua_pushnil(vm.state);
            }
            lua_pushlstring(vm.state, arguments.data(), arguments.size());
            lua_pushlstring(vm.state, raw.data(), raw.size());
            protected_call(vm, 3, 0, std::string("command '") + key + "'");
        }
    }
}

void Runtime::log(ScriptVm& vm, std::string_view text) {
    const auto parsed = parse_level(text);
    const auto plugin_line =
        format_log_line(parsed.level, vm.name, parsed.message);
    const auto console_line =
        format_log_line(parsed.level, "lua:" + vm.name, parsed.message);

    append_line(vm.log_path, plugin_line);
    append_line(core_log_path_, console_line);
    if (console_writer_) console_writer_(console_line);
}

void Runtime::log_runtime(std::string_view text) {
    const auto parsed = parse_level(text);
    const auto line = format_log_line(parsed.level, "lua", parsed.message);
    append_line(core_log_path_, line);
    if (console_writer_) console_writer_(line);
}

std::filesystem::path Runtime::create_log_path() const {
    const auto now_time =
        std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm local{};
    localtime_s(&local, &now_time);
    for (std::uint32_t index = 1; index < 1000000; ++index) {
        std::ostringstream name;
        name << local.tm_mday << (local.tm_mon + 1) << (local.tm_year + 1900)
             << local.tm_hour << local.tm_min << local.tm_sec << index
             << ".log";
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
