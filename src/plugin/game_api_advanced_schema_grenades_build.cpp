// Use current CS2 grenade schema fields for projectile initialization, fuses,
// detonation, and subclass-specific state. The verified trace/final grenade
// layers remain the base.
#include "game_api_advanced_verified_build.cpp"

namespace {

bool set_vector_property(LuaCSAdvancedApi* api, int entity_index,
                         const char* property, const luacs::Vector3& value,
                         std::string& error) {
    PropertyValue property_value;
    property_value.kind = PropertyKind::Vector;
    property_value.vector_value = value;
    return api->property_set(entity_index, property, -1, property_value, true,
                             error);
}

bool set_bool_property(LuaCSAdvancedApi* api, int entity_index,
                       const char* property, bool value,
                       std::string& error) {
    PropertyValue property_value;
    property_value.kind = PropertyKind::Boolean;
    property_value.boolean_value = value;
    return api->property_set(entity_index, property, -1, property_value, true,
                             error);
}

bool set_float_property(LuaCSAdvancedApi* api, int entity_index,
                        const char* property, float value,
                        std::string& error) {
    if (!std::isfinite(value)) {
        error = std::string(property) + " must be finite";
        return false;
    }
    PropertyValue property_value;
    property_value.kind = PropertyKind::Float;
    property_value.float_value = value;
    return api->property_set(entity_index, property, -1, property_value, true,
                             error);
}

bool integer_fits_signed(int value, std::uint16_t width) {
    switch (width) {
        case 1:
            return value >= std::numeric_limits<std::int8_t>::min() &&
                   value <= std::numeric_limits<std::int8_t>::max();
        case 2:
            return value >= std::numeric_limits<std::int16_t>::min() &&
                   value <= std::numeric_limits<std::int16_t>::max();
        case 4:
        case 8:
            return true;
        default:
            return false;
    }
}

bool integer_fits_unsigned(int value, std::uint16_t width) {
    if (value < 0) return false;
    switch (width) {
        case 1:
            return static_cast<unsigned int>(value) <=
                   std::numeric_limits<std::uint8_t>::max();
        case 2:
            return static_cast<unsigned int>(value) <=
                   std::numeric_limits<std::uint16_t>::max();
        case 4:
        case 8:
            return true;
        default:
            return false;
    }
}

bool set_integer_property(LuaCSAdvancedApi* api, int entity_index,
                          const char* property, int value,
                          std::string& error) {
    PropertyInfo info;
    if (!api->property_info(entity_index, property, info, error)) {
        return false;
    }

    PropertyValue property_value;
    property_value.width = static_cast<std::uint8_t>(info.element_size);
    switch (info.kind) {
        case PropertyKind::SignedInteger:
            if (!integer_fits_signed(value, info.element_size)) {
                error = std::string(property) +
                        " value does not fit its signed schema width";
                return false;
            }
            property_value.kind = PropertyKind::SignedInteger;
            property_value.signed_value = value;
            break;
        case PropertyKind::UnsignedInteger:
            if (!integer_fits_unsigned(value, info.element_size)) {
                error = std::string(property) +
                        " value does not fit its unsigned schema width";
                return false;
            }
            property_value.kind = PropertyKind::UnsignedInteger;
            property_value.unsigned_value =
                static_cast<std::uint64_t>(value);
            break;
        default:
            error = std::string(property) +
                    " is not an integer schema property";
            return false;
    }
    return api->property_set(entity_index, property, -1, property_value, true,
                             error);
}

bool set_handle_property(LuaCSAdvancedApi* api, int entity_index,
                         const char* property, int target_entity_index,
                         std::string& error) {
    PropertyValue property_value;
    property_value.kind = PropertyKind::EntityHandle;
    property_value.entity_index = target_entity_index;
    return api->property_set(entity_index, property, -1, property_value, true,
                             error);
}

bool initialize_projectile_schema(LuaCSAdvancedApi* api, int entity_index,
                                  const GrenadeSpawnRequest& request,
                                  int thrower_pawn,
                                  std::string& error) {
    if (!set_vector_property(api, entity_index, "m_vInitialPosition",
                             request.position, error) ||
        !set_vector_property(api, entity_index, "m_vInitialVelocity",
                             request.velocity, error) ||
        !set_vector_property(api, entity_index,
                             "m_vecOriginalSpawnLocation", request.position,
                             error) ||
        !set_bool_property(api, entity_index, "m_bIsLive", true, error) ||
        !set_bool_property(api, entity_index, "m_bDetonationRecorded", false,
                           error) ||
        !set_integer_property(api, entity_index, "m_nBounces", 0, error)) {
        return false;
    }

    if (request.type == GrenadeType::Incendiary &&
        !set_bool_property(api, entity_index, "m_bIsIncGrenade", true,
                           error)) {
        return false;
    }

    if (thrower_pawn >= 0) {
        if (!set_handle_property(api, entity_index, "m_hThrower",
                                 thrower_pawn, error) ||
            !set_handle_property(api, entity_index, "m_hOriginalThrower",
                                 thrower_pawn, error)) {
            return false;
        }

        PropertyValue team;
        if (api->optional_value(thrower_pawn, "m_iTeamNum", team) &&
            !set_integer_property(api, entity_index, "m_iTeamNum",
                                  property_integer(team), error)) {
            return false;
        }
    }
    return true;
}

bool schedule_schema_fuse(LuaCSAdvancedApi* api, int entity_index,
                          float fuse_seconds, std::string& error) {
    if (fuse_seconds < 0.0f) return true;

    PropertyValue spawn_time;
    if (!api->optional_value(entity_index, "m_flSpawnTime", spawn_time)) {
        error = "spawned grenade has no m_flSpawnTime schema field";
        return false;
    }
    const float spawn = property_float(spawn_time,
                                       std::numeric_limits<float>::quiet_NaN());
    if (!std::isfinite(spawn)) {
        error = "spawned grenade reported a non-finite spawn time";
        return false;
    }
    const float detonate = spawn + fuse_seconds;
    if (!std::isfinite(detonate)) {
        error = "grenade fuse overflows the Source 2 game-time range";
        return false;
    }
    return set_float_property(api, entity_index, "m_flDetonateTime",
                              detonate, error);
}

bool grenade_get_schema_impl(LuaCSAdvancedApi* api, int entity_index,
                             GrenadeInfo& output, std::string& error) {
    if (!grenade_get_final_impl(api, entity_index, output, error)) {
        return false;
    }

    PropertyValue value;
    if (!output.exploded &&
        api->optional_value(entity_index, "m_bDetonationRecorded", value)) {
        output.exploded = property_bool(value);
    }
    if ((output.type == GrenadeType::Molotov ||
         output.type == GrenadeType::Incendiary) &&
        api->optional_value(entity_index, "m_bDetonated", value)) {
        output.exploded = property_bool(value, output.exploded);
    }
    if (output.type == GrenadeType::Inferno &&
        api->optional_value(entity_index, "m_nFireLifetime", value)) {
        output.lifetime = property_float(value, output.lifetime);
    }
    if (!output.bounce_sound &&
        api->optional_value(entity_index, "m_iszBounceSound", value) &&
        value.kind == PropertyKind::String) {
        output.bounce_sound = value.string_value[0] != '\0';
    }
    return true;
}

bool grenade_get_schema(void* context, int entity_index,
                        GrenadeInfo* output, char* error,
                        std::size_t error_size) {
    if (error && error_size) error[0] = '\0';
    if (!context || !output) {
        write_error(error, error_size,
                    "grenade context or output is null");
        return false;
    }

    std::string message;
    const bool result = grenade_get_schema_impl(
        static_cast<LuaCSAdvancedApi*>(context), entity_index, *output,
        message);
    if (!result) write_error(error, error_size, message);
    return result;
}

bool grenade_at_schema(void* context, int type, std::size_t index,
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
    if (!grenade_get_schema_impl(api, values[index], *output, message)) {
        write_error(error, error_size, message);
        return false;
    }
    return true;
}

bool grenade_spawn_schema_impl(LuaCSAdvancedApi* api,
                               const GrenadeSpawnRequest& request,
                               GrenadeInfo& output, std::string& error) {
    if (!api->ready(error)) return false;
    if (request.type < GrenadeType::HighExplosive ||
        request.type > GrenadeType::Inferno) {
        error = "grenade type is unsupported or out of range";
        return false;
    }
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
    if (request.type == GrenadeType::Inferno &&
        request.fuse_seconds >= 0.0f) {
        error = "inferno is an active fire entity and does not support a projectile fuse";
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

    const char* classname = request.type == GrenadeType::Incendiary
                                ? "molotov_projectile"
                                : complete_grenade_class(request.type);
    if (!classname) {
        error = "grenade type has no current CS2 entity class";
        return false;
    }

    const auto* world = LuaCS_GetWorldServices();
    if (!world || !world->entity_create || !world->entity_spawn ||
        !world->entity_teleport || !world->entity_remove) {
        error = "complete world entity creation services are unavailable";
        return false;
    }

    const int owner = request.owner_entity_index >= 0
                          ? request.owner_entity_index
                          : thrower_pawn;
    if (owner >= 0 && !world->entity_set_owner) {
        error = "world owner service is unavailable";
        return false;
    }

    luacs::EntityInfo created;
    char world_error[256]{};
    if (!world->entity_create(world->context, classname, &created,
                              world_error, sizeof(world_error))) {
        error = world_error[0] ? world_error : "grenade creation failed";
        return false;
    }
    CreatedEntityRollback rollback{world, created.entity_index, true};

    if (!world->entity_teleport(world->context, created.entity_index,
                                &request.position, &request.angles,
                                &request.velocity, world_error,
                                sizeof(world_error))) {
        error = world_error[0] ? world_error : "grenade teleport failed";
        return false;
    }
    if (owner >= 0 &&
        !world->entity_set_owner(world->context, created.entity_index, owner,
                                 world_error, sizeof(world_error))) {
        error = world_error[0] ? world_error
                               : "grenade owner assignment failed";
        return false;
    }

    if (request.type != GrenadeType::Inferno &&
        !initialize_projectile_schema(api, created.entity_index, request,
                                      thrower_pawn, error)) {
        return false;
    }

    if (request.spawn &&
        !world->entity_spawn(world->context, created.entity_index,
                             world_error, sizeof(world_error))) {
        error = world_error[0] ? world_error : "grenade spawn failed";
        return false;
    }
    if (request.spawn && request.fuse_seconds >= 0.0f &&
        !schedule_schema_fuse(api, created.entity_index,
                              request.fuse_seconds, error)) {
        return false;
    }
    if (!grenade_get_schema_impl(api, created.entity_index, output, error)) {
        return false;
    }
    rollback.release();
    return true;
}

bool grenade_spawn_schema(void* context,
                          const GrenadeSpawnRequest* request,
                          GrenadeInfo* output, char* error,
                          std::size_t error_size) {
    if (error && error_size) error[0] = '\0';
    if (!context || !request || !output) {
        write_error(error, error_size,
                    "grenade context, request, or output is null");
        return false;
    }

    std::string message;
    const bool result = grenade_spawn_schema_impl(
        static_cast<LuaCSAdvancedApi*>(context), *request, *output, message);
    if (!result) write_error(error, error_size, message);
    return result;
}

bool grenade_detonate_schema(void* context, int entity_index, char* error,
                             std::size_t error_size) {
    if (error && error_size) error[0] = '\0';
    if (!context) {
        write_error(error, error_size, "grenade context is null");
        return false;
    }

    auto* api = static_cast<LuaCSAdvancedApi*>(context);
    GrenadeInfo info;
    std::string message;
    if (!grenade_get_schema_impl(api, entity_index, info, message)) {
        write_error(error, error_size, message);
        return false;
    }
    if (info.exploded) {
        write_error(error, error_size, "grenade has already detonated");
        return false;
    }

    if (info.type == GrenadeType::Inferno) {
        const auto* world = LuaCS_GetWorldServices();
        char world_error[256]{};
        if (world && world->entity_accept_input &&
            world->entity_accept_input(world->context, entity_index,
                                       "Extinguish", "", entity_index,
                                       entity_index, 0.0f, world_error,
                                       sizeof(world_error))) {
            return true;
        }
        if (world && world->entity_remove &&
            world->entity_remove(world->context, entity_index, world_error,
                                 sizeof(world_error))) {
            return true;
        }
        write_error(error, error_size,
                    world_error[0] ? world_error
                                   : "inferno could not be extinguished or removed");
        return false;
    }

    if (!set_float_property(api, entity_index, "m_flDetonateTime", 0.0f,
                            message)) {
        write_error(error, error_size,
                    message.empty() ? "grenade detonation-time update failed"
                                    : message);
        return false;
    }
    return true;
}

struct SchemaGrenadeRegistration {
    SchemaGrenadeRegistration() {
        auto& services = g_advanced_api.services;
        services.grenade_get = &grenade_get_schema;
        services.grenade_at = &grenade_at_schema;
        services.grenade_spawn = &grenade_spawn_schema;
        services.grenade_detonate = &grenade_detonate_schema;
    }
};

SchemaGrenadeRegistration g_schema_grenade_registration;

} // namespace
