#include "runtime.h"
#include "runtime_helpers.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <algorithm>
#include <cctype>
#include <ctime>
#include <fstream>
#include <sstream>
#include <system_error>

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

std::pair<std::string, std::string> split_first(std::string_view text) {
    const std::string owned = trim(text);
    if (owned.empty()) return {};
    const auto separator = owned.find_first_of(" \t");
    if (separator == std::string::npos) return {owned, {}};
    return {owned.substr(0, separator),
            trim(std::string_view(owned).substr(separator + 1))};
}

std::string lowercase_ascii(std::string value) {
    for (char& character : value) {
        character = static_cast<char>(
            std::tolower(static_cast<unsigned char>(character)));
    }
    return value;
}

std::string clean_plugin_target(std::string_view target) {
    std::string value = trim(target);
    if (value.size() >= 2 &&
        ((value.front() == '"' && value.back() == '"') ||
         (value.front() == '\'' && value.back() == '\''))) {
        value = value.substr(1, value.size() - 2);
    }
    const std::filesystem::path path(value);
    const std::string extension = lowercase_ascii(path.extension().string());
    if (extension == ".lua" || extension == ".smg") {
        value = path.stem().string();
    }
    return trim(value);
}

std::string single_line(std::string_view text) {
    std::string result(text);
    for (char& character : result) {
        if (character == '\r' || character == '\n' || character == '\t') {
            character = ' ';
        }
    }
    return result;
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
    auto [name, arguments] = parse_command(command_line);

    if (slot < 0 && name == "lua") {
        const auto package_paths = [&]() {
            std::vector<std::filesystem::path> paths;
            std::error_code error;
            for (std::filesystem::directory_iterator iterator(plugins_dir_, error),
                 end;
                 iterator != end && !error; iterator.increment(error)) {
                if (!iterator->is_regular_file(error)) continue;
                if (lowercase_ascii(iterator->path().extension().string()) ==
                    ".smg") {
                    paths.push_back(iterator->path());
                }
            }
            std::sort(paths.begin(), paths.end(),
                      [](const auto& left, const auto& right) {
                          return lowercase_ascii(left.filename().string()) <
                                 lowercase_ascii(right.filename().string());
                      });
            return paths;
        }();

        const auto loaded_for_key = [&](std::string_view key) {
            return std::find_if(
                scripts_.begin(), scripts_.end(),
                [&](const std::unique_ptr<ScriptVm>& vm) {
                    return normalize_name(vm->source_path.stem().string()) == key;
                });
        };

        const auto package_for_key = [&](std::string_view key) {
            return std::find_if(
                package_paths.begin(), package_paths.end(),
                [&](const std::filesystem::path& path) {
                    return normalize_name(path.stem().string()) == key;
                });
        };

        struct Candidate {
            std::string key;
            std::filesystem::path path;
        };

        const auto resolve_target = [&](std::string_view target) {
            std::vector<Candidate> matches;
            const std::string cleaned = clean_plugin_target(target);
            const std::string query = normalize_name(cleaned);
            if (query.empty()) return matches;

            const auto add = [&](std::string key,
                                 std::filesystem::path path) {
                if (std::none_of(matches.begin(), matches.end(),
                                 [&](const Candidate& candidate) {
                                     return candidate.key == key;
                                 })) {
                    matches.push_back({std::move(key), std::move(path)});
                }
            };

            for (const auto& vm : scripts_) {
                const std::string key =
                    normalize_name(vm->source_path.stem().string());
                if (query == key || query == normalize_name(vm->name)) {
                    add(key, vm->source_path);
                }
            }
            for (const auto& path : package_paths) {
                const std::string key = normalize_name(path.stem().string());
                if (query == key) add(key, path);
            }
            for (const auto& [key, metadata] : plugin_metadata_cache_) {
                if (query == key || query == normalize_name(metadata.name)) {
                    const auto package = package_for_key(key);
                    add(key, package == package_paths.end()
                                 ? plugins_dir_ / (key + ".smg")
                                 : *package);
                }
            }
            return matches;
        };

        const auto report_resolution_error =
            [&](std::string_view target, const std::vector<Candidate>& matches) {
                if (matches.empty()) {
                    log_runtime("[ERROR] Plugin was not found: " +
                                std::string(target));
                    return;
                }
                std::ostringstream message;
                message << "[ERROR] Plugin name is ambiguous: " << target
                        << ". Matches: ";
                for (std::size_t index = 0; index < matches.size(); ++index) {
                    if (index != 0) message << ", ";
                    message << matches[index].path.filename().string();
                }
                log_runtime(message.str());
            };

        const auto metadata_for = [&](std::string_view key,
                                      const ScriptVm* loaded) {
            PluginMetadata metadata;
            metadata.name = std::string(key);
            metadata.author = "Unknown";
            metadata.version = "Unspecified";
            if (loaded) {
                metadata.name = loaded->name;
                metadata.author = loaded->author;
                metadata.version = loaded->version;
                metadata.description = loaded->description;
            } else if (const auto found = plugin_metadata_cache_.find(
                           std::string(key));
                       found != plugin_metadata_cache_.end()) {
                metadata = found->second;
            }
            return metadata;
        };

        const auto invoke_unload = [&](ScriptVm& vm) {
            lua_getglobal(vm.state, "OnUnload");
            if (lua_isfunction(vm.state, -1)) {
                return protected_call(vm, 0, 0, "plugin OnUnload callback");
            }
            lua_pop(vm.state, 1);

            lua_getglobal(vm.state, "plugin");
            if (!lua_istable(vm.state, -1)) {
                lua_pop(vm.state, 1);
                return true;
            }
            const int plugin_table = lua_absindex(vm.state, -1);
            lua_getfield(vm.state, plugin_table, "unload");
            if (!lua_isfunction(vm.state, -1)) {
                lua_pop(vm.state, 2);
                return true;
            }
            lua_pushvalue(vm.state, plugin_table);
            lua_remove(vm.state, plugin_table);
            return protected_call(vm, 1, 0, "plugin.unload callback");
        };

        const auto unload_key = [&](std::string_view key, bool force) {
            auto loaded = loaded_for_key(key);
            if (loaded == scripts_.end()) {
                log_runtime("[ERROR] Plugin is not loaded: " +
                            std::string(key));
                return false;
            }
            ScriptVm& vm = **loaded;
            if (!force && !invoke_unload(vm)) {
                log_runtime("[ERROR] Plugin unload was refused because its "
                            "unload callback failed: " + vm.name);
                return false;
            }
            const std::string display_name = vm.name;
            state_map_.erase(vm.state);
            scripts_.erase(loaded);
            plugin_failures_.erase(std::string(key));
            log_runtime(std::string(force ? "Force-unloaded plugin '"
                                          : "Unloaded plugin '") +
                        display_name + "'.");
            return true;
        };

        const auto all_keys = [&]() {
            std::vector<std::string> keys;
            const auto add = [&](std::string key) {
                if (std::find(keys.begin(), keys.end(), key) == keys.end()) {
                    keys.push_back(std::move(key));
                }
            };
            for (const auto& path : package_paths) {
                add(normalize_name(path.stem().string()));
            }
            for (const auto& vm : scripts_) {
                add(normalize_name(vm->source_path.stem().string()));
            }
            for (const auto& [key, metadata] : plugin_metadata_cache_) {
                (void)metadata;
                add(key);
            }
            for (const auto& [key, failure] : plugin_failures_) {
                (void)failure;
                add(key);
            }
            std::sort(keys.begin(), keys.end());
            return keys;
        };

        const auto show_plugin_info = [&](std::string_view key) {
            const auto loaded = loaded_for_key(key);
            const ScriptVm* vm =
                loaded == scripts_.end() ? nullptr : loaded->get();
            const auto package = package_for_key(key);
            const bool package_exists = package != package_paths.end();
            const auto failure = plugin_failures_.find(std::string(key));
            const PluginMetadata metadata = metadata_for(key, vm);

            std::string status = "unloaded";
            if (vm) {
                status = "loaded";
            } else if (failure != plugin_failures_.end()) {
                status = "failed";
            } else if (!package_exists) {
                status = "missing";
            }

            const std::string filename =
                vm ? vm->source_path.filename().string()
                   : package_exists ? package->filename().string()
                                    : std::string(key) + ".smg";
            log_runtime("Plugin: " + single_line(metadata.name));
            log_runtime("  File: " + filename + " (source alias: " +
                        std::string(key) + ".lua)");
            log_runtime("  Status: " + status);
            log_runtime("  Version: " + single_line(metadata.version));
            log_runtime("  Author: " + single_line(metadata.author));
            log_runtime("  Description: " +
                        (metadata.description.empty()
                             ? std::string("No description provided")
                             : single_line(metadata.description)));
            if (failure != plugin_failures_.end()) {
                log_runtime("  Last error: " + single_line(failure->second));
            }
        };

        const auto show_root_help = [&]() {
            log_runtime("LuaCS " + std::string(kLuaCSVersion) + " commands:");
            log_runtime("  lua clear");
            log_runtime("  lua version");
            log_runtime("  lua plugins list");
            log_runtime("  lua plugins info [file.lua|file.smg|plugin name]");
            log_runtime("  lua plugins load <file.lua|file.smg|plugin name>");
            log_runtime("  lua plugins unload <file.lua|file.smg|plugin name>");
            log_runtime("  lua plugins refresh <file.lua|file.smg|plugin name>");
            log_runtime("  lua plugins retry");
            log_runtime("  lua plugins force_load <file.lua|file.smg|plugin name>");
            log_runtime("  lua plugins force_unload <file.lua|file.smg|plugin name>");
            log_runtime("  lua help [plugins [command]]");
        };

        const auto show_plugins_help = [&](std::string_view command) {
            const std::string subcommand = normalize_name(command);
            if (subcommand.empty()) {
                log_runtime("Plugin commands accept a compiled .smg filename, "
                            "its original .lua filename, the filename stem, or "
                            "a previously discovered declared plugin name.");
                log_runtime("Use 'lua help plugins <command>' for one command.");
                log_runtime("Commands: list, info, load, unload, refresh, retry, "
                            "force_load, force_unload");
                return;
            }
            if (subcommand == "list") {
                log_runtime("Usage: lua plugins list");
            } else if (subcommand == "info") {
                log_runtime("Usage: lua plugins info "
                            "[file.lua|file.smg|plugin name]");
            } else if (subcommand == "retry") {
                log_runtime("Usage: lua plugins retry");
                log_runtime("Retries every plugin whose most recent load failed.");
            } else if (subcommand == "load" || subcommand == "unload" ||
                       subcommand == "refresh" || subcommand == "force_load" ||
                       subcommand == "force_unload") {
                log_runtime("Usage: lua plugins " + subcommand +
                            " <file.lua|file.smg|plugin name>");
            } else {
                log_runtime("[ERROR] Unknown plugin help topic: " +
                            std::string(command));
            }
        };

        auto [section, section_arguments] = split_first(arguments);
        section = normalize_name(section);
        if (section.empty() || section == "help") {
            if (section == "help") {
                auto [topic, topic_arguments] = split_first(section_arguments);
                topic = normalize_name(topic);
                if (topic == "plugins") {
                    auto [plugin_topic, unused] = split_first(topic_arguments);
                    (void)unused;
                    show_plugins_help(plugin_topic);
                    return;
                }
                if (!topic.empty()) {
                    log_runtime("[ERROR] Unknown help topic: " + topic);
                    return;
                }
            }
            show_root_help();
            return;
        }
        if (section == "clear") {
            if (server_command_) {
                server_command_("clear");
            } else {
                log_runtime("[ERROR] CS2 server-command service is unavailable.");
            }
            return;
        }
        if (section == "version") {
            log_runtime("LuaCS " + std::string(kLuaCSVersion) + " using " +
                        LUA_VERSION + ".");
            return;
        }
        if (section != "plugins") {
            log_runtime("[ERROR] Unknown lua command: " + section +
                        ". Use 'lua help'.");
            return;
        }

        auto [action, target] = split_first(section_arguments);
        action = normalize_name(action);
        if (action.empty() || action == "help") {
            show_plugins_help({});
            return;
        }
        if (action == "list") {
            const auto keys = all_keys();
            if (keys.empty()) {
                log_runtime("No Lua plugins were found.");
                return;
            }
            log_runtime("Lua plugins (" + std::to_string(keys.size()) + "):");
            for (const auto& key : keys) {
                const auto loaded = loaded_for_key(key);
                const ScriptVm* vm =
                    loaded == scripts_.end() ? nullptr : loaded->get();
                const auto failure = plugin_failures_.find(key);
                const auto package = package_for_key(key);
                const PluginMetadata metadata = metadata_for(key, vm);
                std::string status = "unloaded";
                if (vm) {
                    status = "loaded";
                } else if (failure != plugin_failures_.end()) {
                    status = "failed";
                } else if (package == package_paths.end()) {
                    status = "missing";
                }
                const std::string filename =
                    vm ? vm->source_path.filename().string()
                       : package == package_paths.end()
                             ? key + ".smg"
                             : package->filename().string();
                log_runtime("  [" + status + "] " + filename + " | " +
                            single_line(metadata.name) + " v" +
                            single_line(metadata.version) + " | by " +
                            single_line(metadata.author));
            }
            return;
        }
        if (action == "info") {
            if (trim(target).empty()) {
                const auto keys = all_keys();
                if (keys.empty()) {
                    log_runtime("No Lua plugins were found.");
                    return;
                }
                for (std::size_t index = 0; index < keys.size(); ++index) {
                    if (index != 0) log_runtime("---");
                    show_plugin_info(keys[index]);
                }
                return;
            }
            const auto matches = resolve_target(target);
            if (matches.size() != 1) {
                report_resolution_error(target, matches);
                return;
            }
            show_plugin_info(matches.front().key);
            return;
        }
        if (action == "retry") {
            if (!trim(target).empty()) {
                log_runtime("[ERROR] 'lua plugins retry' does not take a target.");
                return;
            }
            std::vector<std::string> failed_keys;
            failed_keys.reserve(plugin_failures_.size());
            for (const auto& [key, failure] : plugin_failures_) {
                (void)failure;
                failed_keys.push_back(key);
            }
            std::sort(failed_keys.begin(), failed_keys.end());
            if (failed_keys.empty()) {
                log_runtime("No failed plugins need to be retried.");
                return;
            }
            std::size_t succeeded{};
            for (const auto& key : failed_keys) {
                if (loaded_for_key(key) != scripts_.end()) {
                    plugin_failures_.erase(key);
                    continue;
                }
                const auto package = package_for_key(key);
                if (package == package_paths.end()) {
                    log_runtime("[ERROR] Cannot retry missing plugin package: " +
                                key + ".smg");
                    continue;
                }
                if (load_plugin(*package)) ++succeeded;
            }
            log_runtime("Retried " + std::to_string(failed_keys.size()) +
                        " failed plugin(s); " + std::to_string(succeeded) +
                        " loaded successfully.");
            return;
        }

        if (action != "load" && action != "unload" &&
            action != "refresh" && action != "force_load" &&
            action != "force_unload") {
            log_runtime("[ERROR] Unknown plugin command: " + action +
                        ". Use 'lua help plugins'.");
            return;
        }
        if (trim(target).empty()) {
            show_plugins_help(action);
            return;
        }
        const auto matches = resolve_target(target);
        if (matches.size() != 1) {
            report_resolution_error(target, matches);
            return;
        }
        const Candidate selected = matches.front();
        const auto loaded = loaded_for_key(selected.key);

        if (action == "unload" || action == "force_unload") {
            if (loaded == scripts_.end()) {
                log_runtime("[ERROR] Plugin is not loaded: " +
                            selected.path.filename().string());
                return;
            }
            unload_key(selected.key, action == "force_unload");
            return;
        }

        std::error_code file_error;
        if (!std::filesystem::is_regular_file(selected.path, file_error) ||
            file_error) {
            log_runtime("[ERROR] Compiled plugin package does not exist: " +
                        selected.path.string());
            return;
        }

        if (action == "load") {
            if (loaded != scripts_.end()) {
                log_runtime("[ERROR] Plugin is already loaded: " +
                            selected.path.filename().string());
                return;
            }
            if (load_plugin(selected.path)) {
                log_runtime("Loaded plugin package " +
                            selected.path.filename().string() + ".");
            }
            return;
        }

        const bool force = action == "force_load";
        if (loaded != scripts_.end() && !unload_key(selected.key, force)) return;
        if (load_plugin(selected.path)) {
            log_runtime(std::string(action == "refresh" ? "Refreshed plugin "
                                                        : "Force-loaded plugin ") +
                        selected.path.filename().string() + ".");
        }
        return;
    }

    const PlayerSnapshot* player = player_snapshot(slot);

    emit("client_command", [&](lua_State* state) {
        lua_newtable(state);
        if (player) {
            push_player(state, *player);
            lua_setfield(state, -2, "player");
        }
        push_string_field(state, "raw", command_line);
    });

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
                int player_slot = -1;
                if (host_operations_.event_get_player_slot &&
                    host_operations_.event_get_player_slot(token, "userid",
                                                           player_slot)) {
                    if (const auto* player = player_snapshot(player_slot)) {
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
