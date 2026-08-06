// Final compatibility layer. The complete adapter supplies full traces and
// transactional grenade operations; this layer models CS2's shared Molotov /
// Incendiary projectile correctly through CMolotovProjectile::m_bIsIncGrenade.
#include "game_api_advanced_complete_build.cpp"

namespace {

bool is_incendiary_projectile(LuaCSAdvancedApi* api, int entity_index) {
    PropertyValue value;
    return api->optional_value(entity_index, "m_bIsIncGrenade", value) &&
           property_bool(value);
}

bool grenade_get_final_impl(LuaCSAdvancedApi* api, int entity_index,
                            GrenadeInfo& output, std::string& error) {
    if (!grenade_get_complete_impl(api, entity_index, output, error)) {
        return false;
    }
    if (output.type == GrenadeType::Molotov &&
        is_incendiary_projectile(api, entity_index)) {
        output.type = GrenadeType::Incendiary;
    }
    return true;
}

std::vector<int> final_grenade_list(LuaCSAdvancedApi* api, int type,
                                    std::string& error) {
    if (type < static_cast<int>(GrenadeType::Unknown) ||
        type > static_cast<int>(GrenadeType::Inferno)) {
        error = "grenade type is out of range";
        return {};
    }

    const auto candidates = complete_grenade_list(
        api, static_cast<int>(GrenadeType::Unknown), error);
    if (!error.empty()) return {};

    std::vector<int> output;
    output.reserve(candidates.size());
    for (const int entity_index : candidates) {
        GrenadeInfo info;
        std::string inspect_error;
        if (!grenade_get_final_impl(api, entity_index, info, inspect_error)) {
            error = inspect_error;
            return {};
        }
        if (type == static_cast<int>(GrenadeType::Unknown) ||
            type == static_cast<int>(info.type)) {
            output.push_back(entity_index);
        }
    }
    return output;
}

bool grenade_get_final(void* context, int entity_index, GrenadeInfo* output,
                       char* error, std::size_t error_size) {
    if (error && error_size) error[0] = '\0';
    if (!context || !output) {
        write_error(error, error_size,
                    "grenade context or output is null");
        return false;
    }

    std::string message;
    const bool result = grenade_get_final_impl(
        static_cast<LuaCSAdvancedApi*>(context), entity_index, *output,
        message);
    if (!result) write_error(error, error_size, message);
    return result;
}

std::size_t grenade_count_final(void* context, int type, char* error,
                                std::size_t error_size) {
    if (error && error_size) error[0] = '\0';
    if (!context) {
        write_error(error, error_size, "grenade context is null");
        return 0;
    }

    std::string message;
    const auto values = final_grenade_list(
        static_cast<LuaCSAdvancedApi*>(context), type, message);
    if (!message.empty()) write_error(error, error_size, message);
    return values.size();
}

bool grenade_at_final(void* context, int type, std::size_t index,
                      GrenadeInfo* output, char* error,
                      std::size_t error_size) {
    if (error && error_size) error[0] = '\0';
    if (!context || !output) {
        write_error(error, error_size,
                    "grenade context or output is null");
        return false;
    }

    auto* api = static_cast<LuaCSAdvancedApi*>(context);
    std::string message;
    const auto values = final_grenade_list(api, type, message);
    if (!message.empty()) {
        write_error(error, error_size, message);
        return false;
    }
    if (index >= values.size()) {
        write_error(error, error_size,
                    "grenade result index is out of range");
        return false;
    }
    if (!grenade_get_final_impl(api, values[index], *output, message)) {
        write_error(error, error_size, message);
        return false;
    }
    return true;
}

bool grenade_spawn_incendiary(LuaCSAdvancedApi* api,
                              const GrenadeSpawnRequest& request,
                              GrenadeInfo& output, std::string& error) {
    if (!api->ready(error)) return false;
    if (!finite_vector(request.position) ||
        !finite_vector(request.angles) || !finite_vector(request.velocity)) {
        error = "grenade position, angles, and velocity must be finite";
        return false;
    }
    if (!finite_number(request.fuse_seconds) ||
        request.fuse_seconds < -1.0f) {
        error = "grenade fuse must be -1 or a finite non-negative value";
        return false;
    }
    if (!request.spawn && request.fuse_seconds >= 0.0f) {
        error = "a fuse cannot be scheduled on an unspawned grenade";
        return false;
    }
    if (request.owner_entity_index >= 0 &&
        !api->entity(request.owner_entity_index)) {
        error = "grenade owner entity index is invalid";
        return false;
    }

    int thrower_pawn{-1};
    if (request.thrower_slot >= 0 &&
        !pawn_for_slot(api, request.thrower_slot, thrower_pawn, error)) {
        return false;
    }

    const auto* world = LuaCS_GetWorldServices();
    if (!world || !world->entity_create || !world->entity_spawn ||
        !world->entity_teleport || !world->entity_remove) {
        error = "complete world entity creation services are unavailable";
        return false;
    }
    if (request.owner_entity_index >= 0 && !world->entity_set_owner) {
        error = "world owner service is unavailable";
        return false;
    }
    if (request.fuse_seconds >= 0.0f && !world->entity_accept_input) {
        error = "world input service is unavailable for grenade fuse scheduling";
        return false;
    }

    luacs::EntityInfo created;
    char world_error[256]{};
    if (!world->entity_create(world->context, "molotov_projectile", &created,
                              world_error, sizeof(world_error))) {
        error = world_error[0] ? world_error
                               : "incendiary projectile creation failed";
        return false;
    }
    CreatedEntityRollback rollback{world, created.entity_index, true};

    if (!world->entity_teleport(world->context, created.entity_index,
                                &request.position, &request.angles,
                                &request.velocity, world_error,
                                sizeof(world_error))) {
        error = world_error[0] ? world_error
                               : "incendiary projectile teleport failed";
        return false;
    }
    if (request.owner_entity_index >= 0 &&
        !world->entity_set_owner(world->context, created.entity_index,
                                 request.owner_entity_index, world_error,
                                 sizeof(world_error))) {
        error = world_error[0] ? world_error
                               : "incendiary owner assignment failed";
        return false;
    }

    PropertyValue incendiary;
    incendiary.kind = PropertyKind::Boolean;
    incendiary.boolean_value = true;
    if (!api->property_set(created.entity_index, "m_bIsIncGrenade", -1,
                           incendiary, true, error)) {
        if (error.empty()) {
            error = "incendiary discriminator assignment failed";
        }
        return false;
    }

    if (thrower_pawn >= 0) {
        PropertyValue handle;
        handle.kind = PropertyKind::EntityHandle;
        handle.entity_index = thrower_pawn;
        std::string set_error;
        bool set = api->property_set(created.entity_index, "m_hThrower", -1,
                                     handle, true, set_error);
        if (!set) {
            set = api->property_set(created.entity_index,
                                    "m_hOriginalThrower", -1, handle, true,
                                    set_error);
        }
        if (!set) {
            error = set_error.empty() ? "incendiary thrower assignment failed"
                                      : set_error;
            return false;
        }
    }

    if (request.spawn &&
        !world->entity_spawn(world->context, created.entity_index,
                             world_error, sizeof(world_error))) {
        error = world_error[0] ? world_error
                               : "incendiary projectile spawn failed";
        return false;
    }
    if (request.fuse_seconds >= 0.0f &&
        !world->entity_accept_input(
            world->context, created.entity_index, "Detonate", "",
            request.owner_entity_index, request.owner_entity_index,
            request.fuse_seconds, world_error, sizeof(world_error))) {
        error = world_error[0] ? world_error
                               : "incendiary projectile rejected its Detonate input";
        return false;
    }
    if (!grenade_get_final_impl(api, created.entity_index, output, error)) {
        return false;
    }
    rollback.release();
    return true;
}

bool grenade_spawn_final(void* context, const GrenadeSpawnRequest* request,
                         GrenadeInfo* output, char* error,
                         std::size_t error_size) {
    if (error && error_size) error[0] = '\0';
    if (!context || !request || !output) {
        write_error(error, error_size,
                    "grenade context, request, or output is null");
        return false;
    }

    auto* api = static_cast<LuaCSAdvancedApi*>(context);
    std::string message;
    bool result{};
    if (request->type == GrenadeType::Incendiary) {
        result = grenade_spawn_incendiary(api, *request, *output, message);
    } else {
        result = grenade_spawn_complete_impl(api, *request, *output, message);
        if (result && output->type == GrenadeType::Molotov &&
            is_incendiary_projectile(api, output->entity_index)) {
            output->type = GrenadeType::Incendiary;
        }
    }
    if (!result) write_error(error, error_size, message);
    return result;
}

struct FinalAdvancedWorldRegistration {
    FinalAdvancedWorldRegistration() {
        auto& services = g_advanced_api.services;
        services.grenade_get = &grenade_get_final;
        services.grenade_count = &grenade_count_final;
        services.grenade_at = &grenade_at_final;
        services.grenade_spawn = &grenade_spawn_final;
    }
};

FinalAdvancedWorldRegistration g_final_advanced_world_registration;

} // namespace
