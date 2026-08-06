#include "runtime.h"
#include "runtime_helpers.h"

#include <algorithm>
#include <cstdio>
#include <string>

namespace luacs {
using namespace detail;

namespace {

void copy_text(char* destination, std::size_t capacity, std::string_view value) {
    if (!destination || capacity == 0) return;
    std::snprintf(destination, capacity, "%.*s", static_cast<int>(value.size()),
                  value.data());
}

void clear_text(char* destination, std::size_t capacity) {
    if (destination && capacity != 0) destination[0] = '\0';
}

} // namespace

Runtime* Runtime::from_services(void* context) { return static_cast<Runtime*>(context); }

void Runtime::set_host_operations(HostOperations host_operations) {
    host_operations_ = std::move(host_operations);
    services_.hud_print = &Runtime::service_hud_print;
    services_.cvar_exists = &Runtime::service_cvar_exists;
    services_.cvar_get = &Runtime::service_cvar_get;
    services_.cvar_set = &Runtime::service_cvar_set;
    services_.weapon_give = &Runtime::service_weapon_give;
    services_.weapon_remove_all = &Runtime::service_weapon_remove_all;
    services_.weapon_drop_active = &Runtime::service_weapon_drop_active;
}

void Runtime::service_log(void* context, lua_State* state, const char* text) {
    auto* runtime = from_services(context);
    if (auto* vm = runtime->find_vm(state)) runtime->log(*vm, text ? text : "");
}

double Runtime::service_now(void* context) { return from_services(context)->now(); }

bool Runtime::service_event_on(void* context, lua_State* state, const char* event_name,
                               int callback_index, EventCallbackMode mode) {
    auto* runtime = from_services(context);
    auto* vm = runtime->find_vm(state);
    if (!vm || !event_name || !lua_isfunction(state, callback_index)) return false;
    lua_pushvalue(state, callback_index);
    vm->events[normalize_name(event_name)].push_back({luaL_ref(state, LUA_REGISTRYINDEX), mode});
    return true;
}

std::uint64_t Runtime::service_timer_add(void* context, lua_State* state, double delay_seconds,
                                         bool repeat, int callback_index) {
    auto* runtime = from_services(context);
    auto* vm = runtime->find_vm(state);
    if (!vm || !lua_isfunction(state, callback_index) || delay_seconds < 0.0) return 0;
    lua_pushvalue(state, callback_index);
    const int reference = luaL_ref(state, LUA_REGISTRYINDEX);
    const auto id = runtime->next_timer_id_++;
    vm->timers.push_back({id, reference, runtime->now() + delay_seconds,
                          delay_seconds, repeat, false});
    return id;
}

bool Runtime::service_timer_cancel(void* context, lua_State* state, std::uint64_t timer_id) {
    auto* runtime = from_services(context);
    auto* vm = runtime->find_vm(state);
    if (!vm) return false;
    for (auto& timer : vm->timers) {
        if (timer.id == timer_id) {
            timer.cancelled = true;
            return true;
        }
    }
    return false;
}

bool Runtime::service_player_get(void* context, int slot, PlayerInfo* output) {
    auto* runtime = from_services(context);
    const auto found = runtime->players_.find(slot);
    if (found == runtime->players_.end() || !output) return false;
    const auto& p = found->second;
    *output = {p.slot, p.name.c_str(), p.steam64, p.steam_id.c_str(), p.fake, p.connected, p.active};
    return true;
}

std::size_t Runtime::service_player_count(void* context) {
    return from_services(context)->players_.size();
}

bool Runtime::service_player_at(void* context, std::size_t index, PlayerInfo* output) {
    auto* runtime = from_services(context);
    if (!output || index >= runtime->players_.size()) return false;
    std::vector<int> slots;
    slots.reserve(runtime->players_.size());
    for (const auto& [slot, _] : runtime->players_) slots.push_back(slot);
    std::sort(slots.begin(), slots.end());
    return service_player_get(context, slots[index], output);
}

bool Runtime::service_command_on(void* context, lua_State* state, const char* command_name,
                                 int callback_index) {
    auto* runtime = from_services(context);
    auto* vm = runtime->find_vm(state);
    if (!vm || !command_name || !lua_isfunction(state, callback_index)) return false;
    lua_pushvalue(state, callback_index);
    vm->commands[normalize_name(command_name)].push_back(luaL_ref(state, LUA_REGISTRYINDEX));
    return true;
}

bool Runtime::service_hud_print(void* context, int slot, int destination,
                                const char* message, char* error,
                                std::size_t error_size) {
    clear_text(error, error_size);
    auto* runtime = from_services(context);
    if (!runtime || !runtime->host_operations_.hud_print) {
        copy_text(error, error_size, "HUD service is unavailable.");
        return false;
    }
    std::string operation_error;
    const bool result = runtime->host_operations_.hud_print(
        slot, destination, message ? std::string_view(message) : std::string_view(),
        operation_error);
    if (!result) copy_text(error, error_size, operation_error);
    return result;
}

bool Runtime::service_cvar_exists(void* context, const char* name) {
    auto* runtime = from_services(context);
    return runtime && runtime->host_operations_.cvar_exists && name &&
           runtime->host_operations_.cvar_exists(name);
}

bool Runtime::service_cvar_get(void* context, const char* name, char* output,
                               std::size_t output_size, char* error,
                               std::size_t error_size) {
    clear_text(output, output_size);
    clear_text(error, error_size);
    auto* runtime = from_services(context);
    if (!runtime || !runtime->host_operations_.cvar_get) {
        copy_text(error, error_size, "Cvar service is unavailable.");
        return false;
    }
    std::string value;
    std::string operation_error;
    const bool result = runtime->host_operations_.cvar_get(
        name ? std::string_view(name) : std::string_view(), value, operation_error);
    if (result) {
        copy_text(output, output_size, value);
    } else {
        copy_text(error, error_size, operation_error);
    }
    return result;
}

bool Runtime::service_cvar_set(void* context, const char* name, const char* value,
                               char* error, std::size_t error_size) {
    clear_text(error, error_size);
    auto* runtime = from_services(context);
    if (!runtime || !runtime->host_operations_.cvar_set) {
        copy_text(error, error_size, "Cvar service is unavailable.");
        return false;
    }
    std::string operation_error;
    const bool result = runtime->host_operations_.cvar_set(
        name ? std::string_view(name) : std::string_view(),
        value ? std::string_view(value) : std::string_view(), operation_error);
    if (!result) copy_text(error, error_size, operation_error);
    return result;
}

bool Runtime::service_weapon_give(void* context, int slot, const char* class_name,
                                  char* error, std::size_t error_size) {
    clear_text(error, error_size);
    auto* runtime = from_services(context);
    if (!runtime || !runtime->host_operations_.weapon_give) {
        copy_text(error, error_size, "Weapon service is unavailable.");
        return false;
    }
    std::string operation_error;
    const bool result = runtime->host_operations_.weapon_give(
        slot, class_name ? std::string_view(class_name) : std::string_view(),
        operation_error);
    if (!result) copy_text(error, error_size, operation_error);
    return result;
}

bool Runtime::service_weapon_remove_all(void* context, int slot, char* error,
                                        std::size_t error_size) {
    clear_text(error, error_size);
    auto* runtime = from_services(context);
    if (!runtime || !runtime->host_operations_.weapon_remove_all) {
        copy_text(error, error_size, "Weapon service is unavailable.");
        return false;
    }
    std::string operation_error;
    const bool result = runtime->host_operations_.weapon_remove_all(slot, operation_error);
    if (!result) copy_text(error, error_size, operation_error);
    return result;
}

bool Runtime::service_weapon_drop_active(void* context, int slot, char* error,
                                         std::size_t error_size) {
    clear_text(error, error_size);
    auto* runtime = from_services(context);
    if (!runtime || !runtime->host_operations_.weapon_drop_active) {
        copy_text(error, error_size, "Weapon service is unavailable.");
        return false;
    }
    std::string operation_error;
    const bool result = runtime->host_operations_.weapon_drop_active(slot, operation_error);
    if (!result) copy_text(error, error_size, operation_error);
    return result;
}

} // namespace luacs
