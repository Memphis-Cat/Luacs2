#include "runtime.h"
#include "runtime_helpers.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <exception>
#include <limits>
#include <string>
#include <utility>

namespace luacs {
using namespace detail;

namespace {

void copy_text(char* destination, std::size_t capacity, std::string_view value) {
    if (!destination || capacity == 0) return;
    const std::size_t length = std::min(value.size(), capacity - 1);
    if (length != 0) std::memcpy(destination, value.data(), length);
    destination[length] = '\0';
}

void clear_text(char* destination, std::size_t capacity) {
    if (destination && capacity != 0) destination[0] = '\0';
}

bool valid_slot(int slot) { return slot >= 0 && slot < 64; }

bool finite_vector(const Vector3* value) {
    return !value || (std::isfinite(value->x) && std::isfinite(value->y) &&
                      std::isfinite(value->z));
}

template <typename Operation>
bool call_with_error(Runtime* runtime, Operation&& operation, char* error,
                     std::size_t error_size, std::string_view unavailable) {
    clear_text(error, error_size);
    if (!runtime) {
        copy_text(error, error_size, unavailable);
        return false;
    }
    try {
        std::string operation_error;
        const bool result = operation(operation_error);
        if (!result) {
            copy_text(error, error_size,
                      operation_error.empty() ? unavailable : operation_error);
        }
        return result;
    } catch (const std::exception& exception) {
        copy_text(error, error_size,
                  std::string("LuaCS host operation threw: ") + exception.what());
        return false;
    } catch (...) {
        copy_text(error, error_size, "LuaCS host operation threw an unknown exception");
        return false;
    }
}

} // namespace

Runtime* Runtime::from_services(void* context) {
    return static_cast<Runtime*>(context);
}

const PlayerSnapshot* Runtime::player_snapshot(int slot) const {
    const auto found = players_.find(slot);
    return found == players_.end() ? nullptr : &found->second;
}

void Runtime::set_host_operations(HostOperations host_operations) {
    host_operations_ = std::move(host_operations);

    services_.event_on = &Runtime::service_event_on;
    services_.event_off = &Runtime::service_event_off;
    services_.event_has_key = &Runtime::service_event_has_key;
    services_.event_is_empty = &Runtime::service_event_is_empty;
    services_.event_get_bool = &Runtime::service_event_get_bool;
    services_.event_get_int = &Runtime::service_event_get_int;
    services_.event_get_uint64 = &Runtime::service_event_get_uint64;
    services_.event_get_float = &Runtime::service_event_get_float;
    services_.event_get_string = &Runtime::service_event_get_string;
    services_.event_get_player_slot = &Runtime::service_event_get_player_slot;
    services_.event_get_entity_index = &Runtime::service_event_get_entity_index;
    services_.event_get_pawn_index = &Runtime::service_event_get_pawn_index;
    services_.event_set_bool = &Runtime::service_event_set_bool;
    services_.event_set_int = &Runtime::service_event_set_int;
    services_.event_set_uint64 = &Runtime::service_event_set_uint64;
    services_.event_set_float = &Runtime::service_event_set_float;
    services_.event_set_string = &Runtime::service_event_set_string;
    services_.event_cancel = &Runtime::service_event_cancel;
    services_.event_set_dont_broadcast =
        &Runtime::service_event_set_dont_broadcast;

    services_.player_state = &Runtime::service_player_state;
    services_.player_set_int = &Runtime::service_player_set_int;
    services_.player_set_bool = &Runtime::service_player_set_bool;
    services_.player_teleport = &Runtime::service_player_teleport;
    services_.player_kill = &Runtime::service_player_kill;
    services_.player_respawn = &Runtime::service_player_respawn;
    services_.player_change_team = &Runtime::service_player_change_team;

    services_.hud_print = &Runtime::service_hud_print;
    services_.cvar_exists = &Runtime::service_cvar_exists;
    services_.cvar_get = &Runtime::service_cvar_get;
    services_.cvar_set = &Runtime::service_cvar_set;

    services_.weapon_give = &Runtime::service_weapon_give;
    services_.weapon_remove_all = &Runtime::service_weapon_remove_all;
    services_.weapon_drop_active = &Runtime::service_weapon_drop_active;
    services_.weapon_count = &Runtime::service_weapon_count;
    services_.weapon_at = &Runtime::service_weapon_at;
    services_.weapon_get = &Runtime::service_weapon_get;
    services_.weapon_remove = &Runtime::service_weapon_remove;
    services_.weapon_drop = &Runtime::service_weapon_drop;
    services_.weapon_switch = &Runtime::service_weapon_switch;
    services_.weapon_set_clip = &Runtime::service_weapon_set_clip;
    services_.weapon_set_reserve = &Runtime::service_weapon_set_reserve;
    services_.weapon_get_ammo = &Runtime::service_weapon_get_ammo;
    services_.weapon_set_ammo = &Runtime::service_weapon_set_ammo;
}

void Runtime::service_log(void* context, lua_State* state, const char* text) {
    auto* runtime = from_services(context);
    if (runtime) {
        if (auto* vm = runtime->find_vm(state)) {
            runtime->log(*vm, text ? text : "");
        }
    }
}

double Runtime::service_now(void* context) {
    auto* runtime = from_services(context);
    return runtime ? runtime->now() : 0.0;
}

std::uint64_t Runtime::service_event_on(
    void* context, lua_State* state, const char* event_name, int callback_index,
    EventCallbackMode mode, bool post) {
    auto* runtime = from_services(context);
    auto* vm = runtime ? runtime->find_vm(state) : nullptr;
    if (!vm || !event_name || !event_name[0] ||
        !lua_isfunction(state, callback_index) ||
        runtime->next_event_subscription_id_ == 0 ||
        runtime->next_event_subscription_id_ ==
            std::numeric_limits<std::uint64_t>::max()) {
        return 0;
    }

    const std::string key = normalize_name(event_name);
    if (key.empty()) return 0;

    lua_pushvalue(state, callback_index);
    const int reference = luaL_ref(state, LUA_REGISTRYINDEX);
    const std::uint64_t id = runtime->next_event_subscription_id_;
    try {
        vm->events[key].push_back({id, reference, mode, post});
    } catch (...) {
        luaL_unref(state, LUA_REGISTRYINDEX, reference);
        return 0;
    }
    ++runtime->next_event_subscription_id_;
    return id;
}

bool Runtime::service_event_off(void* context, lua_State* state,
                                std::uint64_t subscription_id) {
    auto* runtime = from_services(context);
    auto* vm = runtime ? runtime->find_vm(state) : nullptr;
    if (!vm || subscription_id == 0) return false;

    for (auto map_it = vm->events.begin(); map_it != vm->events.end(); ++map_it) {
        auto& callbacks = map_it->second;
        for (auto callback_it = callbacks.begin(); callback_it != callbacks.end();
             ++callback_it) {
            if (callback_it->subscription_id != subscription_id) continue;
            luaL_unref(state, LUA_REGISTRYINDEX, callback_it->reference);
            callbacks.erase(callback_it);
            if (callbacks.empty()) vm->events.erase(map_it);
            return true;
        }
    }
    return false;
}

bool Runtime::service_event_has_key(void* context, std::uint64_t token,
                                    const char* key) {
    auto* runtime = from_services(context);
    return runtime && token != 0 && runtime->host_operations_.event_has_key &&
           key && key[0] && runtime->host_operations_.event_has_key(token, key);
}

bool Runtime::service_event_is_empty(void* context, std::uint64_t token,
                                     const char* key) {
    auto* runtime = from_services(context);
    return runtime && token != 0 && runtime->host_operations_.event_is_empty &&
           key && key[0] && runtime->host_operations_.event_is_empty(token, key);
}

bool Runtime::service_event_get_bool(void* context, std::uint64_t token,
                                     const char* key, bool fallback,
                                     bool* output) {
    auto* runtime = from_services(context);
    return output && runtime && token != 0 &&
           runtime->host_operations_.event_get_bool && key && key[0] &&
           runtime->host_operations_.event_get_bool(token, key, fallback, *output);
}

bool Runtime::service_event_get_int(void* context, std::uint64_t token,
                                    const char* key, int fallback, int* output) {
    auto* runtime = from_services(context);
    return output && runtime && token != 0 &&
           runtime->host_operations_.event_get_int && key && key[0] &&
           runtime->host_operations_.event_get_int(token, key, fallback, *output);
}

bool Runtime::service_event_get_uint64(void* context, std::uint64_t token,
                                       const char* key, std::uint64_t fallback,
                                       std::uint64_t* output) {
    auto* runtime = from_services(context);
    return output && runtime && token != 0 &&
           runtime->host_operations_.event_get_uint64 && key && key[0] &&
           runtime->host_operations_.event_get_uint64(token, key, fallback,
                                                       *output);
}

bool Runtime::service_event_get_float(void* context, std::uint64_t token,
                                      const char* key, float fallback,
                                      float* output) {
    auto* runtime = from_services(context);
    if (!output || !runtime || token == 0 || !key || !key[0] ||
        !std::isfinite(fallback) || !runtime->host_operations_.event_get_float) {
        return false;
    }
    if (!runtime->host_operations_.event_get_float(token, key, fallback,
                                                    *output)) {
        return false;
    }
    return std::isfinite(*output);
}

bool Runtime::service_event_get_string(void* context, std::uint64_t token,
                                       const char* key, const char* fallback,
                                       char* output,
                                       std::size_t output_size) {
    clear_text(output, output_size);
    auto* runtime = from_services(context);
    if (!runtime || token == 0 || !runtime->host_operations_.event_get_string ||
        !key || !key[0] || !output || output_size == 0) {
        return false;
    }
    try {
        std::string value;
        if (!runtime->host_operations_.event_get_string(
                token, key,
                fallback ? std::string_view(fallback) : std::string_view(),
                value)) {
            return false;
        }
        copy_text(output, output_size, value);
        return true;
    } catch (...) {
        return false;
    }
}

bool Runtime::service_event_get_player_slot(void* context, std::uint64_t token,
                                            const char* key, int* output) {
    auto* runtime = from_services(context);
    if (!output || !runtime || token == 0 || !key || !key[0] ||
        !runtime->host_operations_.event_get_player_slot ||
        !runtime->host_operations_.event_get_player_slot(token, key, *output)) {
        return false;
    }
    return valid_slot(*output);
}

bool Runtime::service_event_get_entity_index(void* context, std::uint64_t token,
                                             const char* key, int* output) {
    auto* runtime = from_services(context);
    return output && runtime && token != 0 && key && key[0] &&
           runtime->host_operations_.event_get_entity_index &&
           runtime->host_operations_.event_get_entity_index(token, key, *output) &&
           *output >= -1;
}

bool Runtime::service_event_get_pawn_index(void* context, std::uint64_t token,
                                           const char* key, int* output) {
    auto* runtime = from_services(context);
    return output && runtime && token != 0 && key && key[0] &&
           runtime->host_operations_.event_get_pawn_index &&
           runtime->host_operations_.event_get_pawn_index(token, key, *output) &&
           *output >= -1;
}

bool Runtime::service_event_set_bool(void* context, std::uint64_t token,
                                     const char* key, bool value) {
    auto* runtime = from_services(context);
    return runtime && token != 0 && runtime->host_operations_.event_set_bool &&
           key && key[0] &&
           runtime->host_operations_.event_set_bool(token, key, value);
}

bool Runtime::service_event_set_int(void* context, std::uint64_t token,
                                    const char* key, int value) {
    auto* runtime = from_services(context);
    return runtime && token != 0 && runtime->host_operations_.event_set_int &&
           key && key[0] &&
           runtime->host_operations_.event_set_int(token, key, value);
}

bool Runtime::service_event_set_uint64(void* context, std::uint64_t token,
                                       const char* key, std::uint64_t value) {
    auto* runtime = from_services(context);
    return runtime && token != 0 && runtime->host_operations_.event_set_uint64 &&
           key && key[0] &&
           runtime->host_operations_.event_set_uint64(token, key, value);
}

bool Runtime::service_event_set_float(void* context, std::uint64_t token,
                                      const char* key, float value) {
    auto* runtime = from_services(context);
    return runtime && token != 0 && std::isfinite(value) &&
           runtime->host_operations_.event_set_float && key && key[0] &&
           runtime->host_operations_.event_set_float(token, key, value);
}

bool Runtime::service_event_set_string(void* context, std::uint64_t token,
                                       const char* key, const char* value) {
    auto* runtime = from_services(context);
    return runtime && token != 0 && runtime->host_operations_.event_set_string &&
           key && key[0] && value &&
           runtime->host_operations_.event_set_string(token, key, value);
}

bool Runtime::service_event_cancel(void* context, std::uint64_t token) {
    auto* runtime = from_services(context);
    return runtime && token != 0 && runtime->host_operations_.event_cancel &&
           runtime->host_operations_.event_cancel(token);
}

bool Runtime::service_event_set_dont_broadcast(void* context,
                                               std::uint64_t token,
                                               bool value) {
    auto* runtime = from_services(context);
    return runtime && token != 0 &&
           runtime->host_operations_.event_set_dont_broadcast &&
           runtime->host_operations_.event_set_dont_broadcast(token, value);
}

std::uint64_t Runtime::service_timer_add(void* context, lua_State* state,
                                         double delay_seconds, bool repeat,
                                         int callback_index) {
    auto* runtime = from_services(context);
    auto* vm = runtime ? runtime->find_vm(state) : nullptr;
    if (!vm || !lua_isfunction(state, callback_index) ||
        !std::isfinite(delay_seconds) || delay_seconds < 0.0 ||
        runtime->next_timer_id_ == 0 ||
        runtime->next_timer_id_ == std::numeric_limits<std::uint64_t>::max()) {
        return 0;
    }
    const double current = runtime->now();
    const double due = current + delay_seconds;
    if (!std::isfinite(current) || !std::isfinite(due)) return 0;

    lua_pushvalue(state, callback_index);
    const int reference = luaL_ref(state, LUA_REGISTRYINDEX);
    const auto id = runtime->next_timer_id_;
    try {
        vm->timers.push_back(
            {id, reference, due, delay_seconds, repeat, false});
    } catch (...) {
        luaL_unref(state, LUA_REGISTRYINDEX, reference);
        return 0;
    }
    ++runtime->next_timer_id_;
    return id;
}

bool Runtime::service_timer_cancel(void* context, lua_State* state,
                                   std::uint64_t timer_id) {
    auto* runtime = from_services(context);
    auto* vm = runtime ? runtime->find_vm(state) : nullptr;
    if (!vm || timer_id == 0) return false;
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
    const auto* player =
        runtime && valid_slot(slot) ? runtime->player_snapshot(slot) : nullptr;
    if (!player || !output) return false;
    *output = {player->slot,      player->name.c_str(), player->steam64,
               player->steam_id.c_str(), player->fake, player->connected,
               player->active};
    return true;
}

std::size_t Runtime::service_player_count(void* context) {
    auto* runtime = from_services(context);
    return runtime ? runtime->players_.size() : 0;
}

bool Runtime::service_player_at(void* context, std::size_t index,
                                PlayerInfo* output) {
    auto* runtime = from_services(context);
    if (!runtime || !output || index >= runtime->players_.size() ||
        runtime->players_.size() > 64) {
        return false;
    }
    std::array<int, 64> slots{};
    std::size_t count = 0;
    for (const auto& [slot, snapshot] : runtime->players_) {
        (void)snapshot;
        if (!valid_slot(slot)) return false;
        slots[count++] = slot;
    }
    std::sort(slots.begin(), slots.begin() + static_cast<std::ptrdiff_t>(count));
    return service_player_get(context, slots[index], output);
}

bool Runtime::service_player_state(void* context, int slot, PlayerState* output,
                                   char* error, std::size_t error_size) {
    if (output) *output = {};
    auto* runtime = from_services(context);
    return call_with_error(
        runtime,
        [&](std::string& operation_error) {
            if (!valid_slot(slot)) {
                operation_error = "Player slot is outside 0..63.";
                return false;
            }
            return output && runtime->host_operations_.player_state &&
                   runtime->host_operations_.player_state(slot, *output,
                                                          operation_error);
        },
        error, error_size, "Player state service is unavailable.");
}

bool Runtime::service_player_set_int(void* context, int slot,
                                     PlayerIntField field, int value,
                                     char* error, std::size_t error_size) {
    auto* runtime = from_services(context);
    return call_with_error(
        runtime,
        [&](std::string& operation_error) {
            if (!valid_slot(slot)) {
                operation_error = "Player slot is outside 0..63.";
                return false;
            }
            return runtime->host_operations_.player_set_int &&
                   runtime->host_operations_.player_set_int(
                       slot, field, value, operation_error);
        },
        error, error_size, "Player mutation service is unavailable.");
}

bool Runtime::service_player_set_bool(void* context, int slot,
                                      PlayerBoolField field, bool value,
                                      char* error, std::size_t error_size) {
    auto* runtime = from_services(context);
    return call_with_error(
        runtime,
        [&](std::string& operation_error) {
            if (!valid_slot(slot)) {
                operation_error = "Player slot is outside 0..63.";
                return false;
            }
            return runtime->host_operations_.player_set_bool &&
                   runtime->host_operations_.player_set_bool(
                       slot, field, value, operation_error);
        },
        error, error_size, "Player mutation service is unavailable.");
}

bool Runtime::service_player_teleport(void* context, int slot,
                                      const Vector3* position,
                                      const Vector3* angles,
                                      const Vector3* velocity, char* error,
                                      std::size_t error_size) {
    auto* runtime = from_services(context);
    return call_with_error(
        runtime,
        [&](std::string& operation_error) {
            if (!valid_slot(slot) || !finite_vector(position) ||
                !finite_vector(angles) || !finite_vector(velocity)) {
                operation_error = "Invalid player slot or non-finite teleport vector.";
                return false;
            }
            return runtime->host_operations_.player_teleport &&
                   runtime->host_operations_.player_teleport(
                       slot, position, angles, velocity, operation_error);
        },
        error, error_size, "Player teleport service is unavailable.");
}

bool Runtime::service_player_kill(void* context, int slot, bool explode,
                                  char* error, std::size_t error_size) {
    auto* runtime = from_services(context);
    return call_with_error(
        runtime,
        [&](std::string& operation_error) {
            if (!valid_slot(slot)) {
                operation_error = "Player slot is outside 0..63.";
                return false;
            }
            return runtime->host_operations_.player_kill &&
                   runtime->host_operations_.player_kill(slot, explode,
                                                         operation_error);
        },
        error, error_size, "Player kill service is unavailable.");
}

bool Runtime::service_player_respawn(void* context, int slot, char* error,
                                     std::size_t error_size) {
    auto* runtime = from_services(context);
    return call_with_error(
        runtime,
        [&](std::string& operation_error) {
            if (!valid_slot(slot)) {
                operation_error = "Player slot is outside 0..63.";
                return false;
            }
            return runtime->host_operations_.player_respawn &&
                   runtime->host_operations_.player_respawn(slot,
                                                            operation_error);
        },
        error, error_size, "Player respawn service is unavailable.");
}

bool Runtime::service_player_change_team(void* context, int slot, int team,
                                         bool switch_team, char* error,
                                         std::size_t error_size) {
    auto* runtime = from_services(context);
    return call_with_error(
        runtime,
        [&](std::string& operation_error) {
            if (!valid_slot(slot) || team < 0 || team > 3) {
                operation_error = "Invalid player slot or team.";
                return false;
            }
            return runtime->host_operations_.player_change_team &&
                   runtime->host_operations_.player_change_team(
                       slot, team, switch_team, operation_error);
        },
        error, error_size, "Player team service is unavailable.");
}

bool Runtime::service_command_on(void* context, lua_State* state,
                                 const char* command_name,
                                 int callback_index) {
    auto* runtime = from_services(context);
    auto* vm = runtime ? runtime->find_vm(state) : nullptr;
    if (!vm || !command_name || !command_name[0] ||
        !lua_isfunction(state, callback_index)) {
        return false;
    }
    const std::string key = normalize_name(command_name);
    if (key.empty()) return false;

    lua_pushvalue(state, callback_index);
    const int reference = luaL_ref(state, LUA_REGISTRYINDEX);
    try {
        vm->commands[key].push_back(reference);
    } catch (...) {
        luaL_unref(state, LUA_REGISTRYINDEX, reference);
        return false;
    }
    return true;
}

bool Runtime::service_hud_print(void* context, int slot, int destination,
                                const char* message, char* error,
                                std::size_t error_size) {
    auto* runtime = from_services(context);
    return call_with_error(
        runtime,
        [&](std::string& operation_error) {
            if ((slot < -1 || slot >= 64) || !message) {
                operation_error = "Invalid HUD slot or message.";
                return false;
            }
            return runtime->host_operations_.hud_print &&
                   runtime->host_operations_.hud_print(
                       slot, destination, std::string_view(message),
                       operation_error);
        },
        error, error_size, "HUD service is unavailable.");
}

bool Runtime::service_cvar_exists(void* context, const char* name) {
    auto* runtime = from_services(context);
    if (!runtime || !runtime->host_operations_.cvar_exists || !name || !name[0]) {
        return false;
    }
    try {
        return runtime->host_operations_.cvar_exists(name);
    } catch (...) {
        return false;
    }
}

bool Runtime::service_cvar_get(void* context, const char* name, char* output,
                               std::size_t output_size, char* error,
                               std::size_t error_size) {
    clear_text(output, output_size);
    auto* runtime = from_services(context);
    std::string value;
    const bool result = call_with_error(
        runtime,
        [&](std::string& operation_error) {
            if (!name || !name[0] || !output || output_size == 0) {
                operation_error = "Invalid cvar name or output buffer.";
                return false;
            }
            return runtime->host_operations_.cvar_get &&
                   runtime->host_operations_.cvar_get(name, value,
                                                       operation_error);
        },
        error, error_size, "Cvar service is unavailable.");
    if (result) copy_text(output, output_size, value);
    return result;
}

bool Runtime::service_cvar_set(void* context, const char* name,
                               const char* value, char* error,
                               std::size_t error_size) {
    auto* runtime = from_services(context);
    return call_with_error(
        runtime,
        [&](std::string& operation_error) {
            if (!name || !name[0] || !value) {
                operation_error = "Invalid cvar name or value.";
                return false;
            }
            return runtime->host_operations_.cvar_set &&
                   runtime->host_operations_.cvar_set(name, value,
                                                       operation_error);
        },
        error, error_size, "Cvar service is unavailable.");
}

bool Runtime::service_weapon_give(void* context, int slot,
                                  const char* class_name, WeaponInfo* output,
                                  char* error, std::size_t error_size) {
    if (output) *output = {};
    auto* runtime = from_services(context);
    return call_with_error(
        runtime,
        [&](std::string& operation_error) {
            if (!valid_slot(slot) || !class_name || !class_name[0]) {
                operation_error = "Invalid weapon slot or classname.";
                return false;
            }
            return output && runtime->host_operations_.weapon_give &&
                   runtime->host_operations_.weapon_give(
                       slot, class_name, *output, operation_error);
        },
        error, error_size, "Weapon service is unavailable.");
}

bool Runtime::service_weapon_remove_all(void* context, int slot, char* error,
                                        std::size_t error_size) {
    auto* runtime = from_services(context);
    return call_with_error(
        runtime,
        [&](std::string& operation_error) {
            if (!valid_slot(slot)) {
                operation_error = "Weapon player slot is outside 0..63.";
                return false;
            }
            return runtime->host_operations_.weapon_remove_all &&
                   runtime->host_operations_.weapon_remove_all(slot,
                                                               operation_error);
        },
        error, error_size, "Weapon service is unavailable.");
}

bool Runtime::service_weapon_drop_active(void* context, int slot, char* error,
                                         std::size_t error_size) {
    auto* runtime = from_services(context);
    return call_with_error(
        runtime,
        [&](std::string& operation_error) {
            if (!valid_slot(slot)) {
                operation_error = "Weapon player slot is outside 0..63.";
                return false;
            }
            return runtime->host_operations_.weapon_drop_active &&
                   runtime->host_operations_.weapon_drop_active(slot,
                                                                operation_error);
        },
        error, error_size, "Weapon service is unavailable.");
}

std::size_t Runtime::service_weapon_count(void* context, int slot, char* error,
                                          std::size_t error_size) {
    clear_text(error, error_size);
    auto* runtime = from_services(context);
    if (!runtime || !runtime->host_operations_.weapon_count || !valid_slot(slot)) {
        copy_text(error, error_size, "Weapon inventory service is unavailable or slot is invalid.");
        return 0;
    }
    try {
        std::string operation_error;
        const std::size_t result =
            runtime->host_operations_.weapon_count(slot, operation_error);
        if (!operation_error.empty()) copy_text(error, error_size, operation_error);
        return result;
    } catch (const std::exception& exception) {
        copy_text(error, error_size, exception.what());
        return 0;
    } catch (...) {
        copy_text(error, error_size, "Weapon inventory service threw an unknown exception.");
        return 0;
    }
}

bool Runtime::service_weapon_at(void* context, int slot, std::size_t index,
                                WeaponInfo* output, char* error,
                                std::size_t error_size) {
    if (output) *output = {};
    auto* runtime = from_services(context);
    return call_with_error(
        runtime,
        [&](std::string& operation_error) {
            if (!valid_slot(slot)) {
                operation_error = "Weapon player slot is outside 0..63.";
                return false;
            }
            return output && runtime->host_operations_.weapon_at &&
                   runtime->host_operations_.weapon_at(
                       slot, index, *output, operation_error);
        },
        error, error_size, "Weapon inventory service is unavailable.");
}

bool Runtime::service_weapon_get(void* context, int entity_index,
                                 WeaponInfo* output, char* error,
                                 std::size_t error_size) {
    if (output) *output = {};
    auto* runtime = from_services(context);
    return call_with_error(
        runtime,
        [&](std::string& operation_error) {
            if (entity_index < 0) {
                operation_error = "Weapon entity index is invalid.";
                return false;
            }
            return output && runtime->host_operations_.weapon_get &&
                   runtime->host_operations_.weapon_get(
                       entity_index, *output, operation_error);
        },
        error, error_size, "Weapon service is unavailable.");
}

bool Runtime::service_weapon_remove(void* context, int slot, int entity_index,
                                    bool delete_entity, char* error,
                                    std::size_t error_size) {
    auto* runtime = from_services(context);
    return call_with_error(
        runtime,
        [&](std::string& operation_error) {
            if (!valid_slot(slot) || entity_index < 0) {
                operation_error = "Invalid weapon player slot or entity index.";
                return false;
            }
            return runtime->host_operations_.weapon_remove &&
                   runtime->host_operations_.weapon_remove(
                       slot, entity_index, delete_entity, operation_error);
        },
        error, error_size, "Weapon removal service is unavailable.");
}

bool Runtime::service_weapon_drop(void* context, int slot, int entity_index,
                                  char* error, std::size_t error_size) {
    auto* runtime = from_services(context);
    return call_with_error(
        runtime,
        [&](std::string& operation_error) {
            if (!valid_slot(slot) || entity_index < 0) {
                operation_error = "Invalid weapon player slot or entity index.";
                return false;
            }
            return runtime->host_operations_.weapon_drop &&
                   runtime->host_operations_.weapon_drop(
                       slot, entity_index, operation_error);
        },
        error, error_size, "Weapon drop service is unavailable.");
}

bool Runtime::service_weapon_switch(void* context, int slot, int entity_index,
                                    char* error, std::size_t error_size) {
    auto* runtime = from_services(context);
    return call_with_error(
        runtime,
        [&](std::string& operation_error) {
            if (!valid_slot(slot) || entity_index < 0) {
                operation_error = "Invalid weapon player slot or entity index.";
                return false;
            }
            return runtime->host_operations_.weapon_switch &&
                   runtime->host_operations_.weapon_switch(
                       slot, entity_index, operation_error);
        },
        error, error_size, "Weapon switch service is unavailable.");
}

bool Runtime::service_weapon_set_clip(void* context, int entity_index,
                                      int clip_index, int value, char* error,
                                      std::size_t error_size) {
    auto* runtime = from_services(context);
    return call_with_error(
        runtime,
        [&](std::string& operation_error) {
            if (entity_index < 0 || clip_index < 0 || clip_index > 1 ||
                value < -1) {
                operation_error = "Invalid weapon clip arguments.";
                return false;
            }
            return runtime->host_operations_.weapon_set_clip &&
                   runtime->host_operations_.weapon_set_clip(
                       entity_index, clip_index, value, operation_error);
        },
        error, error_size, "Weapon clip service is unavailable.");
}

bool Runtime::service_weapon_set_reserve(void* context, int entity_index,
                                         int reserve_index, int value,
                                         char* error,
                                         std::size_t error_size) {
    auto* runtime = from_services(context);
    return call_with_error(
        runtime,
        [&](std::string& operation_error) {
            if (entity_index < 0 || reserve_index < 0 || reserve_index > 1 ||
                value < 0) {
                operation_error = "Invalid weapon reserve-ammo arguments.";
                return false;
            }
            return runtime->host_operations_.weapon_set_reserve &&
                   runtime->host_operations_.weapon_set_reserve(
                       entity_index, reserve_index, value, operation_error);
        },
        error, error_size, "Weapon reserve-ammo service is unavailable.");
}

bool Runtime::service_weapon_get_ammo(void* context, int slot, int ammo_type,
                                      int* output, char* error,
                                      std::size_t error_size) {
    if (output) *output = 0;
    auto* runtime = from_services(context);
    return call_with_error(
        runtime,
        [&](std::string& operation_error) {
            if (!valid_slot(slot) || ammo_type < 0 || ammo_type >= 32) {
                operation_error = "Invalid player slot or ammo type.";
                return false;
            }
            return output && runtime->host_operations_.weapon_get_ammo &&
                   runtime->host_operations_.weapon_get_ammo(
                       slot, ammo_type, *output, operation_error);
        },
        error, error_size, "Player ammo service is unavailable.");
}

bool Runtime::service_weapon_set_ammo(void* context, int slot, int ammo_type,
                                      int value, char* error,
                                      std::size_t error_size) {
    auto* runtime = from_services(context);
    return call_with_error(
        runtime,
        [&](std::string& operation_error) {
            if (!valid_slot(slot) || ammo_type < 0 || ammo_type >= 32 ||
                value < 0 || value > 65535) {
                operation_error = "Invalid player ammo arguments.";
                return false;
            }
            return runtime->host_operations_.weapon_set_ammo &&
                   runtime->host_operations_.weapon_set_ammo(
                       slot, ammo_type, value, operation_error);
        },
        error, error_size, "Player ammo service is unavailable.");
}

} // namespace luacs
