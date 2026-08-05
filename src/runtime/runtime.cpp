#include "runtime.h"
#include "runtime_helpers.h"

#include "smg.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <algorithm>

namespace luacs {
using namespace detail;

Runtime::ScriptVm::~ScriptVm() {
    if (state) lua_close(state);
    for (void* handle : module_handles) {
        if (handle) FreeLibrary(static_cast<HMODULE>(handle));
    }
}

Runtime::Runtime() = default;
Runtime::~Runtime() { shutdown(); }

bool Runtime::initialize(std::filesystem::path root, ConsoleWriter console_writer,
                         ServerCommand server_command, std::string& error) {
    shutdown();
    root_ = std::move(root);
    bin_dir_ = root_ / "bin";
    plugins_dir_ = root_ / "plugins";
    config_dir_ = root_ / "config";
    logs_dir_ = root_ / "logs";
    console_writer_ = std::move(console_writer);
    server_command_ = std::move(server_command);
    started_ = std::chrono::steady_clock::now();

    std::filesystem::create_directories(plugins_dir_);
    std::filesystem::create_directories(config_dir_);
    std::filesystem::create_directories(logs_dir_);

    bool has_compiled_plugins = false;
    for (const auto& entry : std::filesystem::directory_iterator(plugins_dir_)) {
        if (entry.is_regular_file() && entry.path().extension() == ".smg") {
            has_compiled_plugins = true;
            break;
        }
    }
    if (!smg::load_or_create_key(config_dir_ / "luacs.key", !has_compiled_plugins, key_, error)) {
        return false;
    }

    services_.abi_version = kModuleAbiVersion;
    services_.context = this;
    services_.log = &Runtime::service_log;
    services_.now = &Runtime::service_now;
    services_.event_on = &Runtime::service_event_on;
    services_.timer_add = &Runtime::service_timer_add;
    services_.timer_cancel = &Runtime::service_timer_cancel;
    services_.player_get = &Runtime::service_player_get;
    services_.player_count = &Runtime::service_player_count;
    services_.player_at = &Runtime::service_player_at;
    services_.command_on = &Runtime::service_command_on;

    return true;
}

void Runtime::shutdown() {
    state_map_.clear();
    scripts_.clear();
    players_.clear();
    next_timer_id_ = 1;
}

void Runtime::load_plugins() {
    if (!std::filesystem::exists(plugins_dir_)) return;
    std::vector<std::filesystem::path> paths;
    for (const auto& entry : std::filesystem::directory_iterator(plugins_dir_)) {
        if (entry.is_regular_file() && entry.path().extension() == ".smg") paths.push_back(entry.path());
    }
    std::sort(paths.begin(), paths.end());
    for (const auto& path : paths) load_plugin(path);
}

bool Runtime::load_plugin(const std::filesystem::path& path) {
    smg::Package package;
    std::string error;
    if (!smg::read(path, key_, package, error)) {
        log_runtime("[LuaCS] " + error);
        return false;
    }

    auto vm = std::make_unique<ScriptVm>();
    vm->runtime = this;
    vm->source_path = path;
    vm->name = path.stem().string();
    vm->log_path = create_log_path();
    vm->state = luaL_newstate();
    if (!vm->state) {
        log_runtime("[LuaCS] Could not create Lua state for " + vm->name);
        return false;
    }
    luaL_openlibs(vm->state);
    state_map_[vm->state] = vm.get();
    if (!install_core(*vm)) {
        state_map_.erase(vm->state);
        return false;
    }

    const int status = luaL_loadbufferx(vm->state,
                                       reinterpret_cast<const char*>(package.bytecode.data()),
                                       package.bytecode.size(), vm->name.c_str(), "b");
    if (status != LUA_OK) {
        const char* message = lua_tostring(vm->state, -1);
        log(*vm, std::string("load error: ") + (message ? message : "unknown Lua error"));
        state_map_.erase(vm->state);
        return false;
    }
    if (!protected_call(*vm, 0, 0, "plugin startup")) {
        state_map_.erase(vm->state);
        return false;
    }

    log(*vm, "loaded " + vm->name);
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
            if (!timer.cancelled && timer.due <= current) due_ids.push_back(timer.id);
        }

        for (const std::uint64_t id : due_ids) {
            auto found = std::find_if(vm.timers.begin(), vm.timers.end(),
                                      [id](const Timer& timer) { return timer.id == id; });
            if (found == vm.timers.end() || found->cancelled) continue;

            const int reference = found->reference;
            lua_rawgeti(vm.state, LUA_REGISTRYINDEX, reference);
            const bool succeeded = protected_call(vm, 0, 0, "timer callback");

            found = std::find_if(vm.timers.begin(), vm.timers.end(),
                                 [id](const Timer& timer) { return timer.id == id; });
            if (found == vm.timers.end()) continue;
            if (!succeeded) found->cancelled = true;
            if (found->repeat && !found->cancelled) {
                found->due = current + found->interval;
            } else {
                found->cancelled = true;
            }
        }

        vm.timers.erase(std::remove_if(vm.timers.begin(), vm.timers.end(), [&](const Timer& timer) {
            if (!timer.cancelled) return false;
            luaL_unref(vm.state, LUA_REGISTRYINDEX, timer.reference);
            return true;
        }), vm.timers.end());
    }
    emit("game_frame", [&](lua_State* state) {
        lua_newtable(state);
        lua_pushnumber(state, current);
        lua_setfield(state, -2, "time");
    });
}

} // namespace luacs
