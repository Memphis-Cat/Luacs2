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

inline constexpr std::uint32_t kModuleAbiVersion = 4;
inline constexpr std::size_t kClassnameCapacity = 128;

struct WorldServices;

struct Vector3 {
    float x{0.0f};
    float y{0.0f};
    float z{0.0f};
};

struct PlayerInfo {
    int slot{-1};
    const char* name{nullptr};
    std::uint64_t steam64{0};
    const char* steam_id{nullptr};
    bool fake{false};
    bool connected{false};
    bool active{false};
};

struct PlayerState {
    bool valid{false};
    bool has_controller{false};
    bool has_pawn{false};
    bool alive{false};
    int controller_index{-1};
    int pawn_index{-1};
    std::uint32_t pawn_handle{0};
    int health{0};
    int max_health{0};
    int armor{0};
    int team{0};
    int money{0};
    int ping{0};
    bool helmet{false};
    bool defuser{false};
    bool on_ground{false};
    Vector3 position{};
    Vector3 velocity{};
    Vector3 eye_angles{};
};

struct WeaponInfo {
    bool valid{false};
    int entity_index{-1};
    std::uint32_t handle{0};
    int owner_slot{-1};
    bool active{false};
    int clip1{0};
    int clip2{0};
    int reserve1{0};
    int reserve2{0};
    char classname[kClassnameCapacity]{};
};

enum class EventCallbackMode : std::uint32_t {
    EventTable = 0,
    PlayerOnly = 1,
};

enum class HudDestination : int {
    Notify = 1,
    Console = 2,
    Chat = 3,
    Center = 4,
    Alert = 6,
};

enum class PlayerIntField : std::uint32_t {
    Health = 1,
    Armor = 2,
    Money = 3,
};

enum class PlayerBoolField : std::uint32_t {
    Helmet = 1,
    Defuser = 2,
    PreventWeaponPickup = 3,
};

struct Services {
    std::uint32_t abi_version{kModuleAbiVersion};
    void* context{nullptr};

    void (*log)(void* context, lua_State* state, const char* text){nullptr};
    double (*now)(void* context){nullptr};

    std::uint64_t (*event_on)(void* context, lua_State* state,
                              const char* event_name, int callback_index,
                              EventCallbackMode mode, bool post){nullptr};
    bool (*event_off)(void* context, lua_State* state,
                      std::uint64_t subscription_id){nullptr};
    bool (*event_has_key)(void* context, std::uint64_t token,
                          const char* key){nullptr};
    bool (*event_is_empty)(void* context, std::uint64_t token,
                           const char* key){nullptr};
    bool (*event_get_bool)(void* context, std::uint64_t token, const char* key,
                           bool fallback, bool* output){nullptr};
    bool (*event_get_int)(void* context, std::uint64_t token, const char* key,
                          int fallback, int* output){nullptr};
    bool (*event_get_uint64)(void* context, std::uint64_t token,
                             const char* key, std::uint64_t fallback,
                             std::uint64_t* output){nullptr};
    bool (*event_get_float)(void* context, std::uint64_t token, const char* key,
                            float fallback, float* output){nullptr};
    bool (*event_get_string)(void* context, std::uint64_t token,
                             const char* key, const char* fallback,
                             char* output, std::size_t output_size){nullptr};
    bool (*event_get_player_slot)(void* context, std::uint64_t token,
                                  const char* key, int* output){nullptr};
    bool (*event_get_entity_index)(void* context, std::uint64_t token,
                                   const char* key, int* output){nullptr};
    bool (*event_get_pawn_index)(void* context, std::uint64_t token,
                                 const char* key, int* output){nullptr};
    bool (*event_set_bool)(void* context, std::uint64_t token, const char* key,
                           bool value){nullptr};
    bool (*event_set_int)(void* context, std::uint64_t token, const char* key,
                          int value){nullptr};
    bool (*event_set_uint64)(void* context, std::uint64_t token,
                             const char* key, std::uint64_t value){nullptr};
    bool (*event_set_float)(void* context, std::uint64_t token,
                            const char* key, float value){nullptr};
    bool (*event_set_string)(void* context, std::uint64_t token,
                             const char* key, const char* value){nullptr};
    bool (*event_cancel)(void* context, std::uint64_t token){nullptr};
    bool (*event_set_dont_broadcast)(void* context, std::uint64_t token,
                                     bool value){nullptr};

    std::uint64_t (*timer_add)(void* context, lua_State* state,
                               double delay_seconds, bool repeat,
                               int callback_index){nullptr};
    bool (*timer_cancel)(void* context, lua_State* state,
                         std::uint64_t timer_id){nullptr};

    bool (*player_get)(void* context, int slot, PlayerInfo* output){nullptr};
    std::size_t (*player_count)(void* context){nullptr};
    bool (*player_at)(void* context, std::size_t index,
                      PlayerInfo* output){nullptr};
    bool (*player_state)(void* context, int slot, PlayerState* output,
                         char* error, std::size_t error_size){nullptr};
    bool (*player_set_int)(void* context, int slot, PlayerIntField field,
                           int value, char* error,
                           std::size_t error_size){nullptr};
    bool (*player_set_bool)(void* context, int slot, PlayerBoolField field,
                            bool value, char* error,
                            std::size_t error_size){nullptr};
    bool (*player_teleport)(void* context, int slot, const Vector3* position,
                            const Vector3* angles, const Vector3* velocity,
                            char* error, std::size_t error_size){nullptr};
    bool (*player_kill)(void* context, int slot, bool explode,
                        char* error, std::size_t error_size){nullptr};
    bool (*player_respawn)(void* context, int slot, char* error,
                           std::size_t error_size){nullptr};
    bool (*player_change_team)(void* context, int slot, int team,
                               bool switch_team, char* error,
                               std::size_t error_size){nullptr};

    bool (*command_on)(void* context, lua_State* state, const char* command_name,
                       int callback_index){nullptr};

    bool (*hud_print)(void* context, int slot, int destination,
                      const char* message, char* error,
                      std::size_t error_size){nullptr};

    bool (*cvar_exists)(void* context, const char* name){nullptr};
    bool (*cvar_get)(void* context, const char* name, char* output,
                     std::size_t output_size, char* error,
                     std::size_t error_size){nullptr};
    bool (*cvar_set)(void* context, const char* name, const char* value,
                     char* error, std::size_t error_size){nullptr};

    bool (*weapon_give)(void* context, int slot, const char* class_name,
                        WeaponInfo* output, char* error,
                        std::size_t error_size){nullptr};
    bool (*weapon_remove_all)(void* context, int slot, char* error,
                              std::size_t error_size){nullptr};
    bool (*weapon_drop_active)(void* context, int slot, char* error,
                               std::size_t error_size){nullptr};
    std::size_t (*weapon_count)(void* context, int slot, char* error,
                                std::size_t error_size){nullptr};
    bool (*weapon_at)(void* context, int slot, std::size_t index,
                      WeaponInfo* output, char* error,
                      std::size_t error_size){nullptr};
    bool (*weapon_get)(void* context, int entity_index, WeaponInfo* output,
                       char* error, std::size_t error_size){nullptr};
    bool (*weapon_remove)(void* context, int slot, int entity_index,
                          bool delete_entity, char* error,
                          std::size_t error_size){nullptr};
    bool (*weapon_drop)(void* context, int slot, int entity_index, char* error,
                        std::size_t error_size){nullptr};
    bool (*weapon_switch)(void* context, int slot, int entity_index,
                          char* error, std::size_t error_size){nullptr};
    bool (*weapon_set_clip)(void* context, int entity_index, int clip_index,
                            int value, char* error,
                            std::size_t error_size){nullptr};
    bool (*weapon_set_reserve)(void* context, int entity_index,
                               int reserve_index, int value, char* error,
                               std::size_t error_size){nullptr};
    bool (*weapon_get_ammo)(void* context, int slot, int ammo_type,
                            int* output, char* error,
                            std::size_t error_size){nullptr};
    bool (*weapon_set_ammo)(void* context, int slot, int ammo_type,
                            int value, char* error,
                            std::size_t error_size){nullptr};

    const WorldServices* world{nullptr};
};

using OpenModuleFn = int (*)(lua_State* state, const Services* services);

} // namespace luacs
