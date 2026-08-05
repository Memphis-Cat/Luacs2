#pragma once

#include <cstddef>
#include <cstdint>

extern "C" {
#include "lua.h"
}

#if defined(_WIN32)
#  define LUACS_MODULE_EXPORT extern "C" __declspec(dllexport)
#else
#  define LUACS_MODULE_EXPORT extern "C" __attribute__((visibility("default")))
#endif

namespace luacs {

inline constexpr std::uint32_t kModuleAbiVersion = 1;

struct PlayerInfo {
    int slot{-1};
    const char* name{nullptr};
    std::uint64_t steam64{0};
    const char* steam_id{nullptr};
    bool fake{false};
    bool connected{false};
    bool active{false};
};

enum class EventCallbackMode : std::uint32_t {
    EventTable = 0,
    PlayerOnly = 1,
};

struct Services {
    std::uint32_t abi_version{kModuleAbiVersion};
    void* context{nullptr};

    void (*log)(void* context, lua_State* state, const char* text){nullptr};
    double (*now)(void* context){nullptr};

    bool (*event_on)(void* context, lua_State* state, const char* event_name,
                     int callback_index, EventCallbackMode mode){nullptr};
    std::uint64_t (*timer_add)(void* context, lua_State* state, double delay_seconds,
                              bool repeat, int callback_index){nullptr};
    bool (*timer_cancel)(void* context, lua_State* state, std::uint64_t timer_id){nullptr};

    bool (*player_get)(void* context, int slot, PlayerInfo* output){nullptr};
    std::size_t (*player_count)(void* context){nullptr};
    bool (*player_at)(void* context, std::size_t index, PlayerInfo* output){nullptr};

    bool (*command_on)(void* context, lua_State* state, const char* command_name,
                       int callback_index){nullptr};
};

using OpenModuleFn = int (*)(lua_State* state, const Services* services);

} // namespace luacs
