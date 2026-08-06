#pragma once

#include "luacs/module_api.h"

#include <cstddef>
#include <cstdint>

namespace luacs {

inline constexpr std::uint32_t kWorldServicesAbiVersion = 1;
inline constexpr std::size_t kEntityClassnameCapacity = 128;
inline constexpr std::size_t kEntityNameCapacity = 128;
inline constexpr std::size_t kSoundNameCapacity = 256;

struct EntityInfo {
    bool valid{false};
    bool spawned{false};
    int entity_index{-1};
    std::uint32_t handle{0xFFFFFFFFu};
    int health{0};
    int team{0};
    int owner_index{-1};
    int parent_index{-1};
    Vector3 position{};
    Vector3 angles{};
    Vector3 velocity{};
    char classname[kEntityClassnameCapacity]{};
    char name[kEntityNameCapacity]{};
};

struct RoundState {
    bool valid{false};
    bool frozen{false};
    int number{0};
    int win_status{0};
    int win_reason{0};
    float restart_time{0.0f};
};

struct SoundRequest {
    int source_entity_index{0};
    std::uint64_t recipients_mask{0};
    bool has_origin{false};
    Vector3 origin{};
    float volume{1.0f};
    int pitch{100};
    float delay{0.0f};
    int channel{0};
    bool reliable{true};
};

struct SoundInfo {
    bool valid{false};
    std::uint32_t guid{0};
    std::uint32_t stack_hash{0};
    int source_entity_index{-1};
    std::uint64_t recipients_mask{0};
    int channel{0};
    char name[kSoundNameCapacity]{};
};

struct WorldServices {
    std::uint32_t abi_version{kWorldServicesAbiVersion};
    void* context{};

    bool (*team_get_score)(void* context, int team, int* output,
                           char* error, std::size_t error_size){};
    bool (*team_set_score)(void* context, int team, int score,
                           char* error, std::size_t error_size){};

    bool (*round_state)(void* context, RoundState* output,
                        char* error, std::size_t error_size){};
    bool (*round_restart)(void* context, float delay,
                          char* error, std::size_t error_size){};
    bool (*round_terminate)(void* context, float delay, int reason,
                            char* error, std::size_t error_size){};
    bool (*round_set_frozen)(void* context, bool frozen,
                             char* error, std::size_t error_size){};

    bool (*entity_get)(void* context, int entity_index, EntityInfo* output,
                       char* error, std::size_t error_size){};
    std::size_t (*entity_count)(void* context, const char* pattern,
                                bool by_name, char* error,
                                std::size_t error_size){};
    bool (*entity_at)(void* context, const char* pattern, bool by_name,
                      std::size_t index, EntityInfo* output,
                      char* error, std::size_t error_size){};
    bool (*entity_create)(void* context, const char* classname,
                          EntityInfo* output, char* error,
                          std::size_t error_size){};
    bool (*entity_spawn)(void* context, int entity_index,
                         char* error, std::size_t error_size){};
    bool (*entity_remove)(void* context, int entity_index,
                          char* error, std::size_t error_size){};
    bool (*entity_teleport)(void* context, int entity_index,
                            const Vector3* position,
                            const Vector3* angles,
                            const Vector3* velocity,
                            char* error, std::size_t error_size){};
    bool (*entity_set_owner)(void* context, int entity_index,
                             int owner_entity_index, char* error,
                             std::size_t error_size){};
    bool (*entity_set_parent)(void* context, int entity_index,
                              int parent_entity_index, char* error,
                              std::size_t error_size){};
    bool (*entity_accept_input)(void* context, int entity_index,
                                const char* input, const char* value,
                                int activator_entity_index,
                                int caller_entity_index, float delay,
                                char* error, std::size_t error_size){};

    bool (*sound_emit)(void* context, const char* sound_name,
                       const SoundRequest* request, SoundInfo* output,
                       char* error, std::size_t error_size){};
    bool (*sound_stop)(void* context, std::uint32_t guid,
                       std::uint64_t recipients_mask, bool reliable,
                       char* error, std::size_t error_size){};
    std::size_t (*sound_stop_channel)(void* context, int channel,
                                      std::uint64_t recipients_mask,
                                      bool reliable, char* error,
                                      std::size_t error_size){};
};

} // namespace luacs
