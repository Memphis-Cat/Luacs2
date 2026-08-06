#include "runtime.h"
#include "runtime_helpers.h"

#include "smg.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <algorithm>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <regex>
#include <sstream>

namespace luacs {
using namespace detail;

namespace {

std::filesystem::path make_core_log_path(
    const std::filesystem::path& logs_dir) {
    const auto time_value =
        std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm local{};
    localtime_s(&local, &time_value);

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
    if (!std::filesystem::exists(directory, error)) return 0;

    std::size_t count = 0;
    for (std::filesystem::recursive_directory_iterator iterator(
             directory,
             std::filesystem::directory_options::skip_permission_denied,
             error),
         end;
         iterator != end && !error; iterator.increment(error)) {
        if (iterator->is_regular_file(error) &&
            iterator->path().extension().string() == extension) {
            ++count;
        }
    }
    return count;
}

std::size_t count_native_modules(
    const std::filesystem::path& directory) {
    std::error_code error;
    if (!std::filesystem::exists(directory, error)) return 0;

    std::size_t count = 0;
    for (const auto& entry :
         std::filesystem::directory_iterator(directory, error)) {
        if (error) break;
        if (!entry.is_regular_file(error) ||
            entry.path().extension() != ".dll") {
            continue;
        }
        const auto name = entry.path().stem().string();
        if (name != "luacs2" && name != "lua55") ++count;
    }
    return count;
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
    if (!std::filesystem::exists(bin_dir_)) {
        const auto legacy_bin = root_ / "bin";
        if (std::filesystem::exists(legacy_bin)) bin_dir_ = legacy_bin;
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
    std::filesystem::create_directories(config_dir_, filesystem_error);
    if (filesystem_error) {
        error = "Could not create config directory: " +
                filesystem_error.message();
        return false;
    }
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
                    core_log_path_.string();
            return false;
        }
    }

    log_runtime("LuaCS is starting up...");
    log_runtime("Runtime root: " + root_.string());
    log_runtime("Native module directory: " + bin_dir_.string());
    log_runtime("Core log: " + core_log_path_.string());

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

    if (official_entries && std::filesystem::exists(official_path)) {
        log_runtime("Successfully indexed " +
                    std::to_string(*official_entries) +
                    " official game data entries from " +
                    official_path.string());
    } else {
        log_runtime("[WARN] Official game data summary was not available at " +
                    summary_path.string());
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

    if (!std::filesystem::exists(bin_dir_)) {
        error = "Native module directory was not found: " +
                bin_dir_.string();
        log_runtime("[ERROR] " + error);
        return false;
    }
    log_runtime("Discovered " +
                std::to_string(count_native_modules(bin_dir_)) +
                " optional native API modules.");

    bool has_compiled_plugins = false;
    std::size_t compiled_plugin_count = 0;
    for (const auto& entry :
         std::filesystem::directory_iterator(plugins_dir_)) {
        if (entry.is_regular_file() && entry.path().extension() == ".smg") {
            has_compiled_plugins = true;
            ++compiled_plugin_count;
        }
    }

    const auto key_path = config_dir_ / "luacs.key";
    const bool key_existed = std::filesystem::exists(key_path);
    if (!smg::load_or_create_key(key_path, !has_compiled_plugins, key_,
                                 error)) {
        log_runtime("[ERROR] " + error);
        return false;
    }
    log_runtime(std::string(key_existed ? "Loaded" : "Created") +
                " SMG encryption key at " + key_path.string());
    log_runtime("Found " + std::to_string(compiled_plugin_count) +
                " compiled Lua plugin(s) in " + plugins_dir_.string());

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
    if (!std::filesystem::exists(plugins_dir_)) {
        log_runtime("[WARN] Plugins directory does not exist: " +
                    plugins_dir_.string());
        return;
    }

    std::vector<std::filesystem::path> paths;
    for (const auto& entry :
         std::filesystem::directory_iterator(plugins_dir_)) {
        if (entry.is_regular_file() && entry.path().extension() == ".smg") {
            paths.push_back(entry.path());
        }
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
    smg::Package package;
    std::string error;
    if (!smg::read(path, key_, package, error)) {
        log_runtime("[ERROR] Could not load " + path.filename().string() +
                    ": " + error);
        return false;
    }

    auto vm = std::make_unique<ScriptVm>();
    vm->runtime = this;
    vm->source_path = path;
    vm->name = path.stem().string();
    vm->log_path = create_log_path();
    vm->state = luaL_newstate();
    if (!vm->state) {
        log_runtime("[ERROR] Could not create Lua state for " + vm->name);
        return false;
    }

    {
        std::ofstream create_plugin_log(vm->log_path, std::ios::app);
        if (!create_plugin_log) {
            log_runtime("[ERROR] Could not create plugin log " +
                        vm->log_path.string());
            return false;
        }
    }

    luaL_openlibs(vm->state);
    state_map_[vm->state] = vm.get();
    if (!install_core(*vm)) {
        log(*vm, "[ERROR] Core API initialization failed.");
        state_map_.erase(vm->state);
        return false;
    }

    const int status = luaL_loadbufferx(
        vm->state,
        reinterpret_cast<const char*>(package.bytecode.data()),
        package.bytecode.size(), vm->name.c_str(), "b");
    if (status != LUA_OK) {
        const char* message = lua_tostring(vm->state, -1);
        log(*vm, std::string("[ERROR] Bytecode load failed: ") +
                     (message ? message : "unknown Lua error"));
        state_map_.erase(vm->state);
        return false;
    }
    if (!protected_call(*vm, 0, 0, "plugin startup")) {
        state_map_.erase(vm->state);
        return false;
    }

    log(*vm, "Loaded plugin '" + vm->name + "' from " + path.string());
    scripts_.push_back(std::move(vm));
    return true;
}

void Runtime::tick() {
    const double current = now();
    for (auto& vm_ptr : scripts_) {
        auto& vm = *vm_ptr;
        std::vector<std::uint64_t> due_ids;
        due_ids.reserve(vm.timers.size());
        for (const auto& timer : vm.timers) {
            if (!timer.cancelled && timer.due <= current) {
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
                found->due = current + found->interval;
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
