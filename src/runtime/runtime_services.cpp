#include "runtime.h"
#include "runtime_helpers.h"

#include <algorithm>

namespace luacs {
using namespace detail;

Runtime* Runtime::from_services(void* context) { return static_cast<Runtime*>(context); }

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

} // namespace luacs
