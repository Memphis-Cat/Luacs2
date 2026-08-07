#include "luacs/world_api.h"

#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstring>

namespace {

template <typename Function>
Function luacs_resolve_virtual_function(void* instance, std::size_t index) {
    if (!instance) return nullptr;
    auto** vtable = *reinterpret_cast<void***>(instance);
    if (!vtable) return nullptr;
    return reinterpret_cast<Function>(vtable[index]);
}

} // namespace

// The implementation calls the public entity-get operation by its internal
// entity_info method name. WorldServices deliberately exposes both names as a
// union alias, so this translation unit can map the implementation name without
// changing the ABI layout used by modules.
#define entity_get entity_info
#define virtual_function luacs_resolve_virtual_function
#include "game_api_world.cpp"
#undef virtual_function
#undef entity_get

namespace {

using luacs::EntityInfo;
using luacs::RoundState;
using luacs::SoundInfo;
using luacs::SoundRequest;
using luacs::Vector3;
using luacs::WorldServices;

WorldServices g_unchecked_world_services{};

bool reject_checked(char* error, std::size_t error_size,
                    const char* message) {
    if (error && error_size != 0) {
        std::snprintf(error, error_size, "%s", message ? message : "invalid world-service request");
    }
    return false;
}

bool finite_vector_checked(const Vector3& value) {
    return std::isfinite(value.x) && std::isfinite(value.y) &&
           std::isfinite(value.z);
}

template <std::size_t Capacity>
bool terminated_text_checked(const char (&value)[Capacity]) {
    return std::memchr(value, '\0', Capacity) != nullptr;
}

bool valid_entity_info_checked(const EntityInfo& info, int expected_index,
                               char* error, std::size_t error_size) {
    if (!info.valid) {
        return reject_checked(error, error_size,
                              "Source 2 returned an invalid entity result");
    }
    if (info.entity_index < 0 ||
        (expected_index >= 0 && info.entity_index != expected_index)) {
        return reject_checked(error, error_size,
                              "Source 2 returned an inconsistent entity index");
    }
    if (info.handle == 0xFFFFFFFFu) {
        return reject_checked(error, error_size,
                              "Source 2 returned an invalid entity handle");
    }
    if (info.owner_index < -1 || info.parent_index < -1) {
        return reject_checked(error, error_size,
                              "Source 2 returned an invalid entity relationship index");
    }
    if (!finite_vector_checked(info.position) ||
        !finite_vector_checked(info.angles) ||
        !finite_vector_checked(info.velocity)) {
        return reject_checked(error, error_size,
                              "Source 2 returned non-finite entity vectors");
    }
    if (!terminated_text_checked(info.classname) || info.classname[0] == '\0' ||
        !terminated_text_checked(info.name)) {
        return reject_checked(error, error_size,
                              "Source 2 returned malformed entity text fields");
    }
    return true;
}

bool team_get_score_checked(void* context, int team, int* output,
                            char* error, std::size_t error_size) {
    if (team < 1 || team > 3 || !output) {
        return reject_checked(error, error_size,
                              "team score request is invalid");
    }
    const auto callback = g_unchecked_world_services.team_get_score;
    if (!callback) {
        return reject_checked(error, error_size,
                              "team score service is unavailable");
    }
    if (!callback(context, team, output, error, error_size)) return false;
    if (*output < 0) {
        return reject_checked(error, error_size,
                              "Source 2 returned a negative team score");
    }
    return true;
}

bool team_set_score_checked(void* context, int team, int score,
                            char* error, std::size_t error_size) {
    if (team < 1 || team > 3 || score < 0) {
        return reject_checked(error, error_size,
                              "team score update is invalid");
    }
    const auto callback = g_unchecked_world_services.team_set_score;
    return callback ? callback(context, team, score, error, error_size)
                    : reject_checked(error, error_size,
                                     "team score service is unavailable");
}

bool round_state_checked(void* context, RoundState* output,
                         char* error, std::size_t error_size) {
    if (!output) {
        return reject_checked(error, error_size,
                              "round state output is null");
    }
    const auto callback = g_unchecked_world_services.round_state;
    if (!callback) {
        return reject_checked(error, error_size,
                              "round state service is unavailable");
    }
    if (!callback(context, output, error, error_size)) return false;
    if (!output->valid || output->number < 0 ||
        !std::isfinite(output->restart_time)) {
        return reject_checked(error, error_size,
                              "Source 2 returned invalid round state");
    }
    return true;
}

bool round_restart_checked(void* context, float delay,
                           char* error, std::size_t error_size) {
    if (!std::isfinite(delay) || delay < 0.0f) {
        return reject_checked(error, error_size,
                              "round restart delay must be finite and non-negative");
    }
    const auto callback = g_unchecked_world_services.round_restart;
    return callback ? callback(context, delay, error, error_size)
                    : reject_checked(error, error_size,
                                     "round restart service is unavailable");
}

bool round_terminate_checked(void* context, float delay, int reason,
                             char* error, std::size_t error_size) {
    if (!std::isfinite(delay) || delay < 0.0f) {
        return reject_checked(error, error_size,
                              "round termination delay must be finite and non-negative");
    }
    const auto callback = g_unchecked_world_services.round_terminate;
    return callback ? callback(context, delay, reason, error, error_size)
                    : reject_checked(error, error_size,
                                     "round termination service is unavailable");
}

bool round_set_frozen_checked(void* context, bool frozen,
                              char* error, std::size_t error_size) {
    const auto callback = g_unchecked_world_services.round_set_frozen;
    return callback ? callback(context, frozen, error, error_size)
                    : reject_checked(error, error_size,
                                     "round freeze service is unavailable");
}

bool entity_get_checked(void* context, int entity_index, EntityInfo* output,
                        char* error, std::size_t error_size) {
    if (entity_index < 0 || !output) {
        return reject_checked(error, error_size,
                              "entity get request is invalid");
    }
    const auto callback = g_unchecked_world_services.entity_get;
    if (!callback ||
        !callback(context, entity_index, output, error, error_size)) {
        return callback != nullptr ? false
                                   : reject_checked(error, error_size,
                                                    "entity get service is unavailable");
    }
    return valid_entity_info_checked(*output, entity_index, error, error_size);
}

bool entity_at_checked(void* context, const char* pattern, bool by_name,
                       std::size_t index, EntityInfo* output,
                       char* error, std::size_t error_size) {
    if (!pattern || !output) {
        return reject_checked(error, error_size,
                              "entity enumeration request is invalid");
    }
    const auto callback = g_unchecked_world_services.entity_at;
    if (!callback ||
        !callback(context, pattern, by_name, index, output, error, error_size)) {
        return callback != nullptr ? false
                                   : reject_checked(error, error_size,
                                                    "entity enumeration service is unavailable");
    }
    return valid_entity_info_checked(*output, -1, error, error_size);
}

bool entity_create_checked(void* context, const char* classname,
                           EntityInfo* output, char* error,
                           std::size_t error_size) {
    if (!classname || classname[0] == '\0' || !output) {
        return reject_checked(error, error_size,
                              "entity creation request is invalid");
    }
    const auto callback = g_unchecked_world_services.entity_create;
    if (!callback ||
        !callback(context, classname, output, error, error_size)) {
        return callback != nullptr ? false
                                   : reject_checked(error, error_size,
                                                    "entity creation service is unavailable");
    }
    return valid_entity_info_checked(*output, -1, error, error_size);
}

bool entity_spawn_checked(void* context, int entity_index,
                          char* error, std::size_t error_size) {
    if (entity_index < 0) {
        return reject_checked(error, error_size,
                              "entity spawn index is invalid");
    }
    const auto callback = g_unchecked_world_services.entity_spawn;
    return callback ? callback(context, entity_index, error, error_size)
                    : reject_checked(error, error_size,
                                     "entity spawn service is unavailable");
}

bool entity_remove_checked(void* context, int entity_index,
                           char* error, std::size_t error_size) {
    if (entity_index < 0) {
        return reject_checked(error, error_size,
                              "entity removal index is invalid");
    }
    const auto callback = g_unchecked_world_services.entity_remove;
    return callback ? callback(context, entity_index, error, error_size)
                    : reject_checked(error, error_size,
                                     "entity removal service is unavailable");
}

bool entity_teleport_checked(void* context, int entity_index,
                             const Vector3* position,
                             const Vector3* angles,
                             const Vector3* velocity,
                             char* error, std::size_t error_size) {
    if (entity_index < 0 ||
        (position && !finite_vector_checked(*position)) ||
        (angles && !finite_vector_checked(*angles)) ||
        (velocity && !finite_vector_checked(*velocity))) {
        return reject_checked(error, error_size,
                              "entity teleport request contains an invalid index or non-finite vector");
    }
    const auto callback = g_unchecked_world_services.entity_teleport;
    return callback ? callback(context, entity_index, position, angles, velocity,
                               error, error_size)
                    : reject_checked(error, error_size,
                                     "entity teleport service is unavailable");
}

bool entity_set_owner_checked(void* context, int entity_index,
                              int owner_entity_index, char* error,
                              std::size_t error_size) {
    if (entity_index < 0 || owner_entity_index < -1) {
        return reject_checked(error, error_size,
                              "entity owner request contains an invalid index");
    }
    const auto callback = g_unchecked_world_services.entity_set_owner;
    return callback ? callback(context, entity_index, owner_entity_index,
                               error, error_size)
                    : reject_checked(error, error_size,
                                     "entity owner service is unavailable");
}

bool entity_set_parent_checked(void* context, int entity_index,
                               int parent_entity_index, char* error,
                               std::size_t error_size) {
    if (entity_index < 0 || parent_entity_index < -1) {
        return reject_checked(error, error_size,
                              "entity parent request contains an invalid index");
    }
    const auto callback = g_unchecked_world_services.entity_set_parent;
    return callback ? callback(context, entity_index, parent_entity_index,
                               error, error_size)
                    : reject_checked(error, error_size,
                                     "entity parent service is unavailable");
}

bool entity_accept_input_checked(void* context, int entity_index,
                                 const char* input, const char* value,
                                 int activator_entity_index,
                                 int caller_entity_index, float delay,
                                 char* error, std::size_t error_size) {
    if (entity_index < 0 || !input || input[0] == '\0' ||
        activator_entity_index < -1 || caller_entity_index < -1 ||
        !std::isfinite(delay) || delay < 0.0f) {
        return reject_checked(error, error_size,
                              "entity input request is invalid");
    }
    const auto callback = g_unchecked_world_services.entity_accept_input;
    return callback ? callback(context, entity_index, input, value,
                               activator_entity_index, caller_entity_index,
                               delay, error, error_size)
                    : reject_checked(error, error_size,
                                     "entity input service is unavailable");
}

bool valid_sound_info_checked(const SoundInfo& info, char* error,
                              std::size_t error_size) {
    if (!info.valid || info.source_entity_index < -1 ||
        info.recipients_mask == 0 ||
        !terminated_text_checked(info.name) || info.name[0] == '\0') {
        return reject_checked(error, error_size,
                              "Source 2 returned invalid sound state");
    }
    return true;
}

bool sound_emit_checked(void* context, const char* sound_name,
                        const SoundRequest* request, SoundInfo* output,
                        char* error, std::size_t error_size) {
    if (!sound_name || sound_name[0] == '\0' || !request || !output ||
        request->source_entity_index < -1 || request->recipients_mask == 0 ||
        !std::isfinite(request->volume) || request->volume < 0.0f ||
        request->volume > 10.0f || request->pitch < 0 || request->pitch > 255 ||
        !std::isfinite(request->delay) || request->delay < 0.0f ||
        (request->has_origin && !finite_vector_checked(request->origin))) {
        return reject_checked(error, error_size,
                              "sound emission request is invalid");
    }
    const auto callback = g_unchecked_world_services.sound_emit;
    if (!callback ||
        !callback(context, sound_name, request, output, error, error_size)) {
        return callback != nullptr ? false
                                   : reject_checked(error, error_size,
                                                    "sound emission service is unavailable");
    }
    return valid_sound_info_checked(*output, error, error_size);
}

bool sound_stop_checked(void* context, std::uint32_t guid,
                        std::uint64_t recipients_mask, bool reliable,
                        char* error, std::size_t error_size) {
    if (recipients_mask == 0) {
        return reject_checked(error, error_size,
                              "sound stop recipient mask is empty");
    }
    const auto callback = g_unchecked_world_services.sound_stop;
    return callback ? callback(context, guid, recipients_mask, reliable,
                               error, error_size)
                    : reject_checked(error, error_size,
                                     "sound stop service is unavailable");
}

std::size_t sound_stop_channel_checked(void* context, int channel,
                                       std::uint64_t recipients_mask,
                                       bool reliable, char* error,
                                       std::size_t error_size) {
    if (recipients_mask == 0) {
        reject_checked(error, error_size,
                       "sound stop-channel recipient mask is empty");
        return 0;
    }
    const auto callback = g_unchecked_world_services.sound_stop_channel;
    if (!callback) {
        reject_checked(error, error_size,
                       "sound stop-channel service is unavailable");
        return 0;
    }
    return callback(context, channel, recipients_mask, reliable,
                    error, error_size);
}

struct CheckedWorldServicesInstaller {
    CheckedWorldServicesInstaller() {
        g_unchecked_world_services = g_world_api.services;
        auto& services = g_world_api.services;

        services.team_get_score = &team_get_score_checked;
        services.team_set_score = &team_set_score_checked;
        services.round_state = &round_state_checked;
        services.round_restart = &round_restart_checked;
        services.round_terminate = &round_terminate_checked;
        services.round_set_frozen = &round_set_frozen_checked;
        services.entity_get = &entity_get_checked;
        services.entity_at = &entity_at_checked;
        services.entity_create = &entity_create_checked;
        services.entity_spawn = &entity_spawn_checked;
        services.entity_remove = &entity_remove_checked;
        services.entity_teleport = &entity_teleport_checked;
        services.entity_set_owner = &entity_set_owner_checked;
        services.entity_set_parent = &entity_set_parent_checked;
        services.entity_accept_input = &entity_accept_input_checked;
        services.sound_emit = &sound_emit_checked;
        services.sound_stop = &sound_stop_checked;
        services.sound_stop_channel = &sound_stop_channel_checked;
    }
};

CheckedWorldServicesInstaller g_checked_world_services_installer;

} // namespace
