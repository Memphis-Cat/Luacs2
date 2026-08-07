#include "runtime.h"
#include "runtime_helpers.h"

#include "smg.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <algorithm>
#include <cmath>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <regex>
#include <sstream>
#include <system_error>

namespace luacs {
using namespace detail;

namespace {

std::string path_text(const std::filesystem::path& path) {
    const auto utf8 = path.u8string();
    return {reinterpret_cast<const char*>(utf8.data()), utf8.size()};
}

bool path_exists(const std::filesystem::path& path, bool& exists,
                 std::string& error) {
    std::error_code filesystem_error;
    exists = std::filesystem::exists(path, filesystem_error);
    if (filesystem_error) {
        error = "Could not inspect path '" + path_text(path) + "': " +
                filesystem_error.message();
        return false;
    }
    return true;
}

std::filesystem::path make_core_log_path(
    const std::filesystem::path& logs_dir) {
    const auto time_value =
        std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm local{};
    if (localtime_s(&local, &time_value) != 0) local = {};

    std::ostringstream name;
    name << "luacs-" << std::put_time(&local, "%Y%m%d-%H%M%S") << ".log";
    return logs_dir / name.str();
}

std::optional<std::uint64_t> read_json_number(
    const std::filesystem::path& path, std::string_view field) {
    std::ifstream input(path, std::ios::binary);
    if (!input) return std::nullopt;

    std::ostringstream contents;
    contents << input.rdbuf();
    if (!input.eof() && input.fail()) return std::nullopt;
    const std::regex expression("\"" + std::string(field) +
                                "\"\\s*:\\s*([0-9]+)");
    std::smatch match;
    const std::string text = contents.str();
    if (!std::regex_search(text, match, expression) || match.size() < 2) {
        return std::nullopt;
    }

    try {
        return std::stoull(match[1].str());
    } catch (...) {
        return std::nullopt;
    }
}

std::size_t count_files_with_extension(
    const std::filesystem::path& directory, std::string_view extension) {
    std::error_code error;
    if (!std::filesystem::exists(directory, error) || error) return 0;

    std::size_t count = 0;
    for (std::filesystem::recursive_directory_iterator iterator(
             directory,
             std::filesystem::directory_options::skip_permission_denied,
             error),
         end;
         iterator != end && !error; iterator.increment(error)) {
        std::error_code entry_error;
        if (iterator->is_regular_file(entry_error) && !entry_error &&
            iterator->path().extension().string() == extension) {
            ++count;
        }
    }
    return count;
}

std::size_t count_native_modules(
    const std::filesystem::path& directory) {
    std::error_code error;
    if (!std::filesystem::exists(directory, error) || error) return 0;

    std::size_t count = 0;
    std::filesystem::directory_iterator iterator(directory, error);
    const std::filesystem::directory_iterator end;
    for (; iterator != end && !error; iterator.increment(error)) {
        std::error_code entry_error;
        if (!iterator->is_regular_file(entry_error) || entry_error ||
            iterator->path().extension() != ".dll") {
            continue;
        }
        const auto name = iterator->path().stem().string();
        if (name != "luacs2" && name != "lua55") ++count;
    }
    return count;
}

std::string truncate_utf8(std::string_view value, std::size_t maximum) {
    if (value.size() <= maximum) return std::string(value);
    std::size_t end = maximum;
    while (end > 0 &&
           (static_cast<unsigned char>(value[end]) & 0xC0u) == 0x80u) {
        --end;
    }
    if (end == 0) return {};
    const unsigned char lead = static_cast<unsigned char>(value[end]);
    std::size_t sequence_length = 1;
    if ((lead & 0xE0u) == 0xC0u) sequence_length = 2;
    else if ((lead & 0xF0u) == 0xE0u) sequence_length = 3;
    else if ((lead & 0xF8u) == 0xF0u) sequence_length = 4;
    if (end + sequence_length > maximum) return std::string(value.substr(0, end));
    return std::string(value.substr(0, maximum));
}

} // namespace

Runtime::ScriptVm::~ScriptVm() {
    if (state) lua_close(state);
    for (void* handle : module_handles) {
        if (handle) FreeLibrary(static_cast<HMODULE>(handle));
    }
}

Runtime::Runtime() = default;
Runtime::~Runtime() { shutdown(); }

bool Runtime::initialize(std::filesystem::path root,
                         ConsoleWriter console_writer,
                         ServerCommand server_command, std::string& error) {
    shutdown();
    root_ = std::move(root);
    bin_dir_ = root_ / "bin" / "win64";
    bool bin_exists = false;
    if (!path_exists(bin_dir_, bin_exists, error)) return false;
    if (!bin_exists) {
        const auto legacy_bin = root_ / "bin";
        bool legacy_exists = false;
        if (!path_exists(legacy_bin, legacy_exists, error)) return false;
        if (legacy_exists) bin_dir_ = legacy_bin;
    }
    plugins_dir_ = root_ / "plugins";
    config_dir_ = root_ / "config";
    logs_dir_ = root_ / "logs";
    console_writer_ = std::move(console_writer);
    server_command_ = std::move(server_command);
    started_ = std::chrono::steady_clock::now();

    std::error_code filesystem_error;
    std::filesystem::create_directories(plugins_dir_, filesystem_error);
    if (filesystem_error) {
        error = "Could not create plugins directory: " +
                filesystem_error.message();
        return false;
    }
    filesystem_error.clear();
    std::filesystem::create_directories(config_dir_, filesystem_error);
    if (filesystem_error) {
        error = "Could not create config directory: " +
                filesystem_error.message();
        return false;
    }
    filesystem_error.clear();
    std::filesystem::create_directories(logs_dir_, filesystem_error);
    if (filesystem_error) {
        error = "Could not create logs directory: " +
                filesystem_error.message();
        return false;
    }

    core_log_path_ = make_core_log_path(logs_dir_);
    {
        std::ofstream create_log(core_log_path_, std::ios::app);
        if (!create_log) {
            error = "Could not create core log file: " +
                    path_text(core_log_path_);
            return false;
        }
    }

    log_runtime("LuaCS is starting up...");
    log_runtime("Runtime root: " + path_text(root_));
    log_runtime("Native module directory: " + path_text(bin_dir_));
    log_runtime("Core log: " + path_text(core_log_path_));

    const auto reference_dir = root_ / "gamedata" / "reference";
    const auto summary_path = reference_dir / "PACK_SUMMARY.json";
    const auto official_path =
        reference_dir / "official_windows_gamedata.json";
    const auto official_entries =
        read_json_number(summary_path, "official_entries");
    const auto windows_entries =
        read_json_number(summary_path, "windows_only_ready_made_entries");
    const auto json_files =
        count_files_with_extension(reference_dir, ".json");

    bool official_exists = false;
    std::string official_error;
    const bool official_checked =
        path_exists(official_path, official_exists, official_error);
    if (official_entries && official_checked && official_exists) {
        log_runtime("Successfully indexed " +
                    std::to_string(*official_entries) +
                    " official game data entries from " +
                    path_text(official_path));
    } else {
        if (!official_checked) log_runtime("[WARN] " + official_error);
        log_runtime("[WARN] Official game data summary was not available at " +
                    path_text(summary_path));
    }
    if (windows_entries) {
        log_runtime("Game data/reference pack contains " +
                    std::to_string(*windows_entries) +
                    " Windows-ready entries across " +
                    std::to_string(json_files) + " JSON files.");
    } else {
        log_runtime("Found " + std::to_string(json_files) +
                    " game data/reference JSON files.");
    }

    if (!path_exists(bin_dir_, bin_exists, error)) return false;
    if (!bin_exists) {
        error = "Native module directory was not found: " +
                path_text(bin_dir_);
        log_runtime("[ERROR] " + error);
        return false;
    }
    log_runtime("Discovered " +
                std::to_string(count_native_modules(bin_dir_)) +
                " optional native API modules.");

    bool has_compiled_plugins = false;
    std::size_t compiled_plugin_count = 0;
    std::error_code plugin_iterator_error;
    std::filesystem::directory_iterator plugin_iterator(plugins_dir_,
                                                         plugin_iterator_error);
    const std::filesystem::directory_iterator plugin_end;
    for (; plugin_iterator != plugin_end && !plugin_iterator_error;
         plugin_iterator.increment(plugin_iterator_error)) {
        std::error_code entry_error;
        if (plugin_iterator->is_regular_file(entry_error) && !entry_error &&
            plugin_iterator->path().extension() == ".smg") {
            has_compiled_plugins = true;
            ++compiled_plugin_count;
        }
    }
    if (plugin_iterator_error) {
        error = "Could not enumerate plugins directory: " +
                plugin_iterator_error.message();
        log_runtime("[ERROR] " + error);
        return false;
    }

    const auto key_path = config_dir_ / "luacs.key";
    bool key_existed = false;
    if (!path_exists(key_path, key_existed, error)) {
        log_runtime("[ERROR] " + error);
        return false;
    }
    if (!smg::load_or_create_key(key_path, !has_compiled_plugins, key_,
                                 error)) {
        log_runtime("[ERROR] " + error);
        return false;
    }
    log_runtime(std::string(key_existed ? "Loaded" : "Created") +
                " SMG encryption key at " + path_text(key_path));
    log_runtime("Found " + std::to_string(compiled_plugin_count) +
                " compiled Lua plugin(s) in " + path_text(plugins_dir_));

    services_ = {};
    services_.abi_version = kModuleAbiVersion;
    services_.context = this;
    services_.log = &Runtime::service_log;
    services_.now = &Runtime::service_now;
    services_.timer_add = &Runtime::service_timer_add;
    services_.timer_cancel = &Runtime::service_timer_cancel;
    services_.player_get = &Runtime::service_player_get;
    services_.player_count = &Runtime::service_player_count;
    services_.player_at = &Runtime::service_player_at;
    services_.command_on = &Runtime::service_command_on;

    log_runtime("Lua 5.5 runtime services initialized with ABI v" +
                std::to_string(kModuleAbiVersion) + ".");
    return true;
}

void Runtime::shutdown() {
    if (!root_.empty()) log_runtime("LuaCS is shutting down.");
    state_map_.clear();
    scripts_.clear();
    players_.clear();
    plugin_metadata_cache_.clear();
    plugin_failures_.clear();
    next_timer_id_ = 1;
    next_event_subscription_id_ = 1;
    host_operations_ = {};
    services_ = {};
    root_.clear();
    bin_dir_.clear();
    plugins_dir_.clear();
    config_dir_.clear();
    logs_dir_.clear();
    core_log_path_.clear();
    console_writer_ = {};
    server_command_ = {};
}

void Runtime::load_plugins() {
    std::string path_error;
    bool plugins_exist = false;
    if (!path_exists(plugins_dir_, plugins_exist, path_error)) {
        log_runtime("[ERROR] " + path_error);
        return;
    }
    if (!plugins_exist) {
        log_runtime("[WARN] Plugins directory does not exist: " +
                    path_text(plugins_dir_));
        return;
    }

    std::vector<std::filesystem::path> paths;
    std::error_code iterator_error;
    std::filesystem::directory_iterator iterator(plugins_dir_, iterator_error);
    const std::filesystem::directory_iterator end;
    for (; iterator != end && !iterator_error;
         iterator.increment(iterator_error)) {
        std::error_code entry_error;
        if (iterator->is_regular_file(entry_error) && !entry_error &&
            iterator->path().extension() == ".smg") {
            paths.push_back(iterator->path());
        }
    }
    if (iterator_error) {
        log_runtime("[ERROR] Could not enumerate plugins directory: " +
                    iterator_error.message());
        return;
    }
    std::sort(paths.begin(), paths.end());

    const auto before = scripts_.size();
    for (const auto& path : paths) load_plugin(path);
    const auto loaded = scripts_.size() - before;

    if (paths.empty()) {
        log_runtime(
            "[WARN] No compiled .smg plugins were found. Run "
            "scripting\\compile.exe.");
    } else if (loaded == paths.size()) {
        log_runtime("Successfully loaded all " + std::to_string(loaded) +
                    " compiled Lua plugin(s).");
    } else {
        log_runtime("[WARN] Loaded " + std::to_string(loaded) + " of " +
                    std::to_string(paths.size()) +
                    " compiled Lua plugin(s). Check the log above for "
                    "errors.");
    }
}

bool Runtime::load_plugin(const std::filesystem::path& path) {
    const std::string key = normalize_name(path_text(path.stem()));
    const auto fail = [&](std::string message) {
        plugin_failures_[key] = message;
        log_runtime("[ERROR] Could not load " + path_text(path.filename()) +
                    ": " + message);
        return false;
    };

    smg::Package package;
    std::string error;
    if (!smg::read(path, key_, package, error)) return fail(error);
    if (package.bytecode.empty()) {
        return fail("authenticated SMG package contains empty Lua bytecode");
    }

    auto vm = std::make_unique<ScriptVm>();
    vm->runtime = this;
    vm->source_path = path;
    vm->name = path_text(path.stem());
    vm->author = "Unknown";
    vm->version = "Unspecified";
    vm->log_path = create_log_path();
    vm->state = luaL_newstate();
    if (!vm->state) return fail("could not create a Lua state");

    {
        std::ofstream create_plugin_log(vm->log_path, std::ios::app);
        if (!create_plugin_log) {
            return fail("could not create plugin log " +
                        path_text(vm->log_path));
        }
    }

    luaL_openlibs(vm->state);
    state_map_[vm->state] = vm.get();
    if (!install_core(*vm)) {
        log(*vm, "[ERROR] Core API initialization failed.");
        state_map_.erase(vm->state);
        return fail("core API initialization failed; see the plugin log");
    }

    const char* bytecode = reinterpret_cast<const char*>(package.bytecode.data());
    const int status = luaL_loadbufferx(vm->state, bytecode,
                                        package.bytecode.size(), vm->name.c_str(),
                                        "b");
    if (status != LUA_OK) {
        const char* message = lua_tostring(vm->state, -1);
        const std::string detail =
            message ? message : "unknown Lua bytecode error";
        log(*vm, "[ERROR] Bytecode load failed: " + detail);
        state_map_.erase(vm->state);
        return fail("bytecode load failed: " + detail);
    }
    if (!protected_call(*vm, 0, 0, "plugin startup")) {
        state_map_.erase(vm->state);
        return fail("plugin startup failed; see the plugin log");
    }

    read_plugin_metadata(*vm);
    plugin_failures_.erase(key);
    plugin_metadata_cache_[key] =
        PluginMetadata{vm->name, vm->author, vm->version, vm->description};
    log(*vm, "Loaded plugin '" + vm->name + "' from " + path_text(path));
    scripts_.push_back(std::move(vm));
    return true;
}

void Runtime::read_plugin_metadata(ScriptVm& vm) {
    PluginMetadata metadata;
    metadata.name = path_text(vm.source_path.stem());
    metadata.author = "Unknown";
    metadata.version = "Unspecified";

    lua_getglobal(vm.state, "plugin");
    if (lua_istable(vm.state, -1)) {
        const int table = lua_absindex(vm.state, -1);
        const auto read_field = [&](const char* field,
                                    std::size_t maximum) -> std::string {
            lua_getfield(vm.state, table, field);
            std::string value;
            if (lua_type(vm.state, -1) == LUA_TSTRING) {
                std::size_t length{};
                const char* text = lua_tolstring(vm.state, -1, &length);
                if (text && length != 0) {
                    value = truncate_utf8(std::string_view(text, length), maximum);
                }
            }
            lua_pop(vm.state, 1);
            return value;
        };

        if (auto value = read_field("name", 256); !value.empty()) {
            metadata.name = std::move(value);
        }
        if (auto value = read_field("author", 256); !value.empty()) {
            metadata.author = std::move(value);
        }
        if (auto value = read_field("version", 128); !value.empty()) {
            metadata.version = std::move(value);
        }
        metadata.description = read_field("description", 1024);
    }
    lua_pop(vm.state, 1);

    vm.name = std::move(metadata.name);
    vm.author = std::move(metadata.author);
    vm.version = std::move(metadata.version);
    vm.description = std::move(metadata.description);
}

void Runtime::tick() {
    const double current = now();
    if (!std::isfinite(current)) {
        log_runtime("[ERROR] Runtime clock returned a non-finite value; skipping frame.");
        return;
    }
    for (auto& vm_ptr : scripts_) {
        auto& vm = *vm_ptr;
        std::vector<std::uint64_t> due_ids;
        due_ids.reserve(vm.timers.size());
        for (const auto& timer : vm.timers) {
            if (!timer.cancelled && std::isfinite(timer.due) &&
                timer.due <= current) {
                due_ids.push_back(timer.id);
            }
        }

        for (const std::uint64_t id : due_ids) {
            auto found = std::find_if(
                vm.timers.begin(), vm.timers.end(),
                [id](const Timer& timer) { return timer.id == id; });
            if (found == vm.timers.end() || found->cancelled) continue;

            const int reference = found->reference;
            lua_rawgeti(vm.state, LUA_REGISTRYINDEX, reference);
            const bool succeeded =
                protected_call(vm, 0, 0, "timer callback");

            found = std::find_if(
                vm.timers.begin(), vm.timers.end(),
                [id](const Timer& timer) { return timer.id == id; });
            if (found == vm.timers.end()) continue;
            if (!succeeded) found->cancelled = true;
            if (found->repeat && !found->cancelled) {
                const double next_due = current + found->interval;
                if (!std::isfinite(next_due)) {
                    log(*vm, "[ERROR] Repeating timer overflowed its due time and was cancelled.");
                    found->cancelled = true;
                } else {
                    found->due = next_due;
                }
            } else {
                found->cancelled = true;
            }
        }

        vm.timers.erase(
            std::remove_if(
                vm.timers.begin(), vm.timers.end(),
                [&](const Timer& timer) {
                    if (!timer.cancelled) return false;
                    luaL_unref(vm.state, LUA_REGISTRYINDEX,
                               timer.reference);
                    return true;
                }),
            vm.timers.end());
    }
    emit("game_frame", [&](lua_State* state) {
        lua_newtable(state);
        lua_pushnumber(state, current);
        lua_setfield(state, -2, "time");
    });
}

} // namespace luacs
