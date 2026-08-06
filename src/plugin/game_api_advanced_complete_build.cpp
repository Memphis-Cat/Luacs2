// Compile the existing schema-property adapter in this translation unit, then
// replace only the trace and grenade callbacks with the complete ABI v3
// implementations below. Keeping the adapter included (rather than linked as a
// second object) gives this compatibility layer access to the resolved Source 2
// functions without exporting private engine pointers.
#include "game_api_advanced_build.cpp"

namespace {

using luacs::GrenadeInfo;
using luacs::GrenadeSpawnRequest;
using luacs::GrenadeType;
using luacs::PropertyKind;
using luacs::PropertyValue;

bool finite_number(float value) { return std::isfinite(value); }

bool ordered_bounds(const luacs::Vector3& mins, const luacs::Vector3& maxs) {
    return mins.x <= maxs.x && mins.y <= maxs.y && mins.z <= maxs.z;
}

bool same_vector(const luacs::Vector3& left, const luacs::Vector3& right) {
    return left.x == right.x && left.y == right.y && left.z == right.z;
}

Vector native_vector(const luacs::Vector3& value) {
    return Vector(value.x, value.y, value.z);
}

class CompleteTraceFilter final : public CTraceFilter {
public:
    CompleteTraceFilter(std::uint64_t interacts_with, int collision_group,
                        bool iterate_entities)
        : CTraceFilter(interacts_with, collision_group, iterate_entities) {}

    bool add_ignored(CEntityInstance* entity) {
        if (!entity) return false;
        const std::uint32_t handle = entity->GetRefEHandle().ToInt();
        for (std::size_t index = 0; index < ignored_count_; ++index) {
            if (ignored_[index] == handle) return true;
        }
        if (ignored_count_ >= ignored_.size()) return false;
        ignored_[ignored_count_++] = handle;
        if (ignored_count_ == 1) SetPassEntity1(entity);
        if (ignored_count_ == 2) SetPassEntity2(entity);
        return true;
    }

    bool ShouldHitEntity(CEntityInstance* entity) override {
        if (!entity) return true;
        const std::uint32_t handle = entity->GetRefEHandle().ToInt();
        for (std::size_t index = 0; index < ignored_count_; ++index) {
            if (ignored_[index] == handle) return false;
        }
        return true;
    }

    std::size_t ignored_count() const { return ignored_count_; }

private:
    std::array<std::uint32_t, luacs::kTraceIgnoreCapacity> ignored_{};
    std::size_t ignored_count_{};
};

bool add_trace_ignore(LuaCSAdvancedApi* api, CompleteTraceFilter& filter,
                      int entity_index, char* error,
                      std::size_t error_size) {
    if (entity_index < 0) return true;
    CEntityInstance* entity = api->entity(entity_index);
    if (!entity) {
        write_error(error, error_size,
                    "trace ignore list contains an invalid entity index");
        return false;
    }
    if (!filter.add_ignored(entity)) {
        write_error(error, error_size,
                    "trace ignore list exceeds the ABI capacity");
        return false;
    }
    return true;
}

bool build_native_ray(const TraceRequest& request, Ray_t& ray,
                      std::array<Vector, luacs::kTraceMeshVertexCapacity>&
                          mesh_vertices,
                      char* error, std::size_t error_size) {
    switch (request.shape) {
        case TraceShape::Line:
            if (!finite_vector(request.center_a)) {
                write_error(error, error_size,
                            "line trace start offset must be finite");
                return false;
            }
            ray.Init(native_vector(request.center_a));
            return true;

        case TraceShape::Sphere:
            if (!finite_vector(request.center_a) ||
                !finite_number(request.radius) || request.radius <= 0.0f) {
                write_error(error, error_size,
                            "sphere trace requires a finite center and radius greater than zero");
                return false;
            }
            ray.Init(native_vector(request.center_a), request.radius);
            return ray.m_eType == RAY_TYPE_SPHERE;

        case TraceShape::Hull:
            if (!finite_vector(request.mins) ||
                !finite_vector(request.maxs) ||
                !ordered_bounds(request.mins, request.maxs) ||
                same_vector(request.mins, request.maxs)) {
                write_error(error, error_size,
                            "hull trace requires finite ordered non-degenerate bounds");
                return false;
            }
            ray.Init(native_vector(request.mins), native_vector(request.maxs));
            return ray.m_eType == RAY_TYPE_HULL;

        case TraceShape::Capsule:
            if (!finite_vector(request.center_a) ||
                !finite_vector(request.center_b) ||
                same_vector(request.center_a, request.center_b) ||
                !finite_number(request.radius) || request.radius <= 0.0f) {
                write_error(error, error_size,
                            "capsule trace requires two different finite centers and a radius greater than zero");
                return false;
            }
            ray.Init(native_vector(request.center_a),
                     native_vector(request.center_b), request.radius);
            return ray.m_eType == RAY_TYPE_CAPSULE;

        case TraceShape::Mesh:
            if (!finite_vector(request.mins) ||
                !finite_vector(request.maxs) ||
                !ordered_bounds(request.mins, request.maxs) ||
                same_vector(request.mins, request.maxs)) {
                write_error(error, error_size,
                            "mesh trace requires finite ordered non-degenerate bounds");
                return false;
            }
            if (request.mesh_vertex_count < 3 ||
                request.mesh_vertex_count >
                    luacs::kTraceMeshVertexCapacity) {
                write_error(error, error_size,
                            "mesh trace vertex count is outside the supported range");
                return false;
            }
            for (std::size_t index = 0;
                 index < request.mesh_vertex_count; ++index) {
                if (!finite_vector(request.mesh_vertices[index])) {
                    write_error(error, error_size,
                                "mesh trace contains a non-finite vertex");
                    return false;
                }
                mesh_vertices[index] =
                    native_vector(request.mesh_vertices[index]);
            }
            ray.Init(native_vector(request.mins), native_vector(request.maxs),
                     mesh_vertices.data(),
                     static_cast<int>(request.mesh_vertex_count));
            return ray.m_eType == RAY_TYPE_MESH;
    }

    write_error(error, error_size, "trace shape is out of range");
    return false;
}

bool trace_complete(void* context, const TraceRequest* request,
                    TraceResult* output, char* error,
                    std::size_t error_size) {
    if (error && error_size) error[0] = '\0';
    if (!context || !request || !output) {
        write_error(error, error_size,
                    "trace context, request, or output is null");
        return false;
    }
    if (!finite_vector(request->start) || !finite_vector(request->end)) {
        write_error(error, error_size, "trace coordinates must be finite");
        return false;
    }
    if (request->ignore_count > luacs::kTraceIgnoreCapacity) {
        write_error(error, error_size,
                    "trace ignore count exceeds the ABI capacity");
        return false;
    }
    if (request->collision_group > 0xFFu) {
        write_error(error, error_size,
                    "trace collision group exceeds the Source 2 byte range");
        return false;
    }

    auto* api = static_cast<LuaCSAdvancedApi*>(context);
    std::string message;
    if (!api->ready(message)) {
        write_error(error, error_size, message);
        return false;
    }

    TraceRequest normalized = *request;
    if (normalized.use_hull) normalized.shape = TraceShape::Hull;
    if (normalized.shape < TraceShape::Line ||
        normalized.shape > TraceShape::Mesh) {
        write_error(error, error_size, "trace shape is out of range");
        return false;
    }

    const std::uint64_t interacts_with =
        normalized.interacts_with
            ? normalized.interacts_with
            : (normalized.contents ? normalized.contents
                                   : normalized.contents_mask);
    const bool iterate_entities =
        normalized.hit_entities &&
        (normalized.iterate_entities || normalized.ignore_entity_index >= 0 ||
         normalized.ignore_count != 0);

    CompleteTraceFilter filter(
        interacts_with, static_cast<int>(normalized.collision_group),
        iterate_entities);
    filter.m_nInteractsAs = normalized.interacts_as;
    filter.m_nInteractsWith = interacts_with;
    filter.m_nInteractsExclude = normalized.interacts_exclude;
    std::uint8_t object_set = static_cast<std::uint8_t>(
        normalized.ignore_entities_mask
            ? normalized.ignore_entities_mask
            : static_cast<std::uint64_t>(RNQUERY_OBJECTS_ALL));
    if (!normalized.hit_entities) {
        object_set = static_cast<std::uint8_t>(object_set &
                                               RNQUERY_OBJECTS_STATIC);
    }
    filter.m_nObjectSetMask = object_set;
    filter.m_nCollisionGroup =
        static_cast<std::uint8_t>(normalized.collision_group);
    filter.m_nIncludedDetailLayers = normalized.included_detail_layers;
    filter.m_nTargetDetailLayer = normalized.target_detail_layer;
    filter.m_bHitSolid = normalized.hit_solid;
    filter.m_bHitSolidRequiresGenerateContacts =
        normalized.hit_solid_requires_generate_contacts;
    filter.m_bHitTrigger = normalized.hit_triggers;
    filter.m_bShouldIgnoreDisabledPairs =
        normalized.ignore_disabled_pairs;
    filter.m_bIgnoreIfBothInteractWithHitboxes =
        normalized.ignore_if_both_hitboxes;
    filter.m_bForceHitEverything = normalized.force_hit_everything;
    filter.m_bIterateEntities = iterate_entities;

    if (!add_trace_ignore(api, filter, normalized.ignore_entity_index, error,
                          error_size)) {
        return false;
    }
    for (std::size_t index = 0; index < normalized.ignore_count; ++index) {
        if (!add_trace_ignore(api, filter,
                              normalized.ignore_entities[index], error,
                              error_size)) {
            return false;
        }
    }

    Ray_t ray;
    std::array<Vector, luacs::kTraceMeshVertexCapacity> mesh_vertices{};
    if (!build_native_ray(normalized, ray, mesh_vertices, error,
                          error_size)) {
        if (error && error_size && error[0] == '\0') {
            write_error(error, error_size,
                        "Source 2 changed the requested ray to a different shape");
        }
        return false;
    }

    const Vector start = native_vector(normalized.start);
    const Vector end = native_vector(normalized.end);
    CGameTrace native;
    if (!api->trace_shape_(
            api->trace_manager_,
            reinterpret_cast<LuaCSAdvancedApi::NativeRay*>(&ray), &start,
            &end, &filter, &native)) {
        write_error(error, error_size,
                    "Source 2 TraceShape rejected the request");
        return false;
    }

    *output = {};
    output->valid = true;
    output->hit = native.DidHit();
    output->start_solid = native.m_bStartInSolid;
    output->all_solid =
        native.m_bStartInSolid && native.m_flFraction <= 0.0f;
    output->exact_hit_point = native.m_bExactHitPoint;
    output->fraction = native.m_flFraction;
    output->fraction_left_solid = 0.0f;
    output->fraction_left_solid_available = false;
    output->hit_offset = native.m_flHitOffset;
    output->shape = static_cast<TraceShape>(native.m_eRayType);
    output->start = {native.m_vStartPos.x, native.m_vStartPos.y,
                     native.m_vStartPos.z};
    output->end = {native.m_vEndPos.x, native.m_vEndPos.y,
                   native.m_vEndPos.z};
    output->hit_position = {native.m_vHitPoint.x, native.m_vHitPoint.y,
                            native.m_vHitPoint.z};
    output->plane_normal = {native.m_vHitNormal.x, native.m_vHitNormal.y,
                            native.m_vHitNormal.z};
    output->contents64 = native.m_nContents;
    output->contents = static_cast<int>(native.m_nContents);
    output->triangle = native.m_nTriangle;
    output->bone = native.m_nHitboxBoneIndex;
    output->physics_body =
        reinterpret_cast<std::uintptr_t>(native.m_hBody);
    output->physics_shape =
        reinterpret_cast<std::uintptr_t>(native.m_hShape);

    output->shape_interacts_as = native.m_ShapeAttributes.m_nInteractsAs;
    output->shape_interacts_with =
        native.m_ShapeAttributes.m_nInteractsWith;
    output->shape_interacts_exclude =
        native.m_ShapeAttributes.m_nInteractsExclude;
    output->shape_entity_id = native.m_ShapeAttributes.m_nEntityId;
    output->shape_owner_id = native.m_ShapeAttributes.m_nOwnerId;
    output->shape_hierarchy_id =
        native.m_ShapeAttributes.m_nHierarchyId;
    output->shape_detail_layer_mask =
        native.m_ShapeAttributes.m_nDetailLayerMask;
    output->shape_detail_layer_mask_type =
        native.m_ShapeAttributes.m_nDetailLayerMaskType;
    output->shape_target_detail_layer =
        native.m_ShapeAttributes.m_nTargetDetailLayer;
    output->shape_collision_group =
        native.m_ShapeAttributes.m_nCollisionGroup;
    output->shape_collision_function_mask =
        native.m_ShapeAttributes.m_nCollisionFunctionMask;

    const float delta_x = output->hit_position.x - output->start.x;
    const float delta_y = output->hit_position.y - output->start.y;
    const float delta_z = output->hit_position.z - output->start.z;
    output->distance =
        std::sqrt(delta_x * delta_x + delta_y * delta_y +
                  delta_z * delta_z);
    output->plane_distance =
        output->plane_normal.x * output->hit_position.x +
        output->plane_normal.y * output->hit_position.y +
        output->plane_normal.z * output->hit_position.z;

    if (native.m_pEnt && native.m_pEnt->m_pEntity) {
        output->entity_index = native.m_pEnt->GetEntityIndex().Get();
        output->entity_handle = native.m_pEnt->GetRefEHandle().ToInt();
    }
    if (native.m_pHitbox) {
        output->hitbox = native.m_pHitbox->m_nHitBoxIndex;
        output->hitgroup = native.m_pHitbox->m_nGroupId;
    }
    if (native.m_pSurfaceProperties) {
        const char* name = native.m_pSurfaceProperties->m_name.String();
        copy_text(output->surface_name, sizeof(output->surface_name),
                  name ? name : "");
    }
    return true;
}

GrenadeType complete_grenade_type(std::string_view classname) {
    if (classname.find("inferno") != std::string_view::npos)
        return GrenadeType::Inferno;
    if (classname.find("hegrenade") != std::string_view::npos)
        return GrenadeType::HighExplosive;
    if (classname.find("flashbang") != std::string_view::npos)
        return GrenadeType::Flashbang;
    if (classname.find("smokegrenade") != std::string_view::npos)
        return GrenadeType::Smoke;
    if (classname.find("incendiary") != std::string_view::npos ||
        classname.find("incgrenade") != std::string_view::npos)
        return GrenadeType::Incendiary;
    if (classname.find("molotov") != std::string_view::npos)
        return GrenadeType::Molotov;
    if (classname.find("decoy") != std::string_view::npos)
        return GrenadeType::Decoy;
    return GrenadeType::Unknown;
}

const char* complete_grenade_class(GrenadeType type) {
    switch (type) {
        case GrenadeType::HighExplosive: return "hegrenade_projectile";
        case GrenadeType::Flashbang: return "flashbang_projectile";
        case GrenadeType::Smoke: return "smokegrenade_projectile";
        case GrenadeType::Molotov: return "molotov_projectile";
        case GrenadeType::Incendiary:
            return "incendiarygrenade_projectile";
        case GrenadeType::Decoy: return "decoy_projectile";
        case GrenadeType::Inferno: return "inferno";
        default: return nullptr;
    }
}

int property_integer(const PropertyValue& value, int fallback = 0) {
    switch (value.kind) {
        case PropertyKind::SignedInteger:
            return static_cast<int>(value.signed_value);
        case PropertyKind::UnsignedInteger:
        case PropertyKind::Pointer:
            return static_cast<int>(value.unsigned_value);
        case PropertyKind::EntityHandle:
            return value.entity_index;
        case PropertyKind::Float:
            return static_cast<int>(value.float_value);
        default:
            return fallback;
    }
}

float property_float(const PropertyValue& value, float fallback = 0.0f) {
    switch (value.kind) {
        case PropertyKind::Float:
            return static_cast<float>(value.float_value);
        case PropertyKind::SignedInteger:
            return static_cast<float>(value.signed_value);
        case PropertyKind::UnsignedInteger:
            return static_cast<float>(value.unsigned_value);
        default:
            return fallback;
    }
}

bool property_bool(const PropertyValue& value, bool fallback = false) {
    switch (value.kind) {
        case PropertyKind::Boolean:
            return value.boolean_value;
        case PropertyKind::SignedInteger:
            return value.signed_value != 0;
        case PropertyKind::UnsignedInteger:
            return value.unsigned_value != 0;
        default:
            return fallback;
    }
}

bool first_optional(LuaCSAdvancedApi* api, int entity_index,
                    std::initializer_list<const char*> paths,
                    PropertyValue& output) {
    for (const char* path : paths) {
        if (api->optional_value(entity_index, path, output)) return true;
    }
    return false;
}

int slot_for_pawn(LuaCSAdvancedApi* api, int pawn_index) {
    if (pawn_index < 0) return -1;
    auto* system = api->entity_system();
    if (!system) return -1;
    for (CEntityIdentity* identity =
             system->m_EntityList.m_pFirstActiveEntity;
         identity; identity = identity->m_pNext) {
        if (!identity->m_pInstance) continue;
        const char* classname = identity->GetClassname();
        if (!classname ||
            std::string_view(classname).find("player_controller") ==
                std::string_view::npos) {
            continue;
        }
        const int controller_index = identity->GetEntityIndex().Get();
        PropertyValue pawn;
        if (!api->optional_value(controller_index, "m_hPlayerPawn", pawn) ||
            pawn.entity_index != pawn_index) {
            continue;
        }
        PropertyValue slot;
        if (first_optional(api, controller_index,
                           {"m_iPlayerSlot", "m_nPlayerSlot"}, slot)) {
            const int value = property_integer(slot, -1);
            if (value >= 0 && value < 64) return value;
        }
        const int fallback = controller_index - 1;
        return fallback >= 0 && fallback < 64 ? fallback : -1;
    }
    return -1;
}

bool pawn_for_slot(LuaCSAdvancedApi* api, int slot, int& pawn_index,
                   std::string& error) {
    if (slot < 0 || slot >= 64) {
        error = "thrower slot is outside the supported player range";
        return false;
    }
    auto* system = api->entity_system();
    if (!system) {
        error = "game entity system is unavailable";
        return false;
    }
    for (CEntityIdentity* identity =
             system->m_EntityList.m_pFirstActiveEntity;
         identity; identity = identity->m_pNext) {
        if (!identity->m_pInstance) continue;
        const char* classname = identity->GetClassname();
        if (!classname ||
            std::string_view(classname).find("player_controller") ==
                std::string_view::npos) {
            continue;
        }
        const int controller_index = identity->GetEntityIndex().Get();
        PropertyValue slot_value;
        bool matches{};
        if (first_optional(api, controller_index,
                           {"m_iPlayerSlot", "m_nPlayerSlot"},
                           slot_value)) {
            matches = property_integer(slot_value, -1) == slot;
        } else {
            matches = controller_index == slot + 1;
        }
        if (!matches) continue;

        PropertyValue pawn;
        if (!api->optional_value(controller_index, "m_hPlayerPawn", pawn) ||
            pawn.entity_index < 0 || !api->entity(pawn.entity_index)) {
            error = "thrower slot has no live player pawn";
            return false;
        }
        pawn_index = pawn.entity_index;
        return true;
    }
    error = "thrower slot has no player controller";
    return false;
}

bool grenade_get_complete_impl(LuaCSAdvancedApi* api, int entity_index,
                               GrenadeInfo& output, std::string& error) {
    if (!api->ready(error)) return false;
    CEntityInstance* instance = api->entity(entity_index);
    if (!instance || !instance->m_pEntity) {
        error = "grenade entity index is invalid";
        return false;
    }
    const char* raw = instance->GetClassname();
    const GrenadeType kind = complete_grenade_type(raw ? raw : "");
    if (kind == GrenadeType::Unknown) {
        error = "entity is not a supported grenade projectile or inferno";
        return false;
    }

    const auto* world = LuaCS_GetWorldServices();
    luacs::EntityInfo entity_info;
    char world_error[256]{};
    if (!world || !world->entity_get ||
        !world->entity_get(world->context, entity_index, &entity_info,
                           world_error, sizeof(world_error))) {
        error = world_error[0] ? world_error
                               : "world entity query service failed";
        return false;
    }

    output = {};
    output.valid = true;
    output.type = kind;
    output.entity_index = entity_index;
    output.handle = entity_info.handle;
    output.owner_entity_index = entity_info.owner_index;
    output.position = entity_info.position;
    output.velocity = entity_info.velocity;
    copy_text(output.classname, sizeof(output.classname),
              entity_info.classname);

    PropertyValue value;
    if (first_optional(api, entity_index,
                       {"m_flSpawnTime", "m_flCreateTime"}, value)) {
        output.spawn_time = property_float(value);
    }
    if (first_optional(api, entity_index,
                       {"m_flDetonateTime", "m_flDetonationTime"}, value)) {
        output.detonate_time = property_float(value);
    }
    if (first_optional(api, entity_index,
                       {"m_bHasExploded", "m_bExploded"}, value)) {
        output.exploded = property_bool(value);
    }
    if (first_optional(api, entity_index,
                       {"m_bDidSmokeEffect", "m_bSmokeEffectSpawned",
                        "m_bSmokeEffect"},
                       value)) {
        output.smoke_active = property_bool(value);
    }
    if (first_optional(api, entity_index,
                       {"m_hThrower", "m_hOriginalThrower"}, value)) {
        output.thrower_entity_index = property_integer(value, -1);
        output.thrower_slot =
            slot_for_pawn(api, output.thrower_entity_index);
    }
    if (first_optional(api, entity_index,
                       {"m_iTeamNum", "m_iInitialTeamNum"}, value)) {
        output.team = property_integer(value);
    }
    if (first_optional(api, entity_index,
                       {"m_nBounces", "m_nBounceCount"}, value)) {
        output.bounce_count = property_integer(value);
    }
    if (first_optional(api, entity_index,
                       {"m_nFireCount", "m_fireCount"}, value)) {
        output.fire_count = property_integer(value);
    }
    if (first_optional(api, entity_index,
                       {"m_flLifetime", "m_flLifeTime"}, value)) {
        output.lifetime = property_float(value);
    } else if (output.spawn_time > 0.0f &&
               output.detonate_time >= output.spawn_time) {
        output.lifetime = output.detonate_time - output.spawn_time;
    }
    if (first_optional(api, entity_index,
                       {"m_nSmokeEffectTickBegin", "m_nSmokeEffectTick"},
                       value)) {
        output.smoke_effect_tick = property_float(value);
    }
    if (first_optional(api, entity_index,
                       {"m_bBounceSound", "m_bHasBounceSound"}, value)) {
        output.bounce_sound = property_bool(value);
    }
    return true;
}

std::vector<int> complete_grenade_list(LuaCSAdvancedApi* api, int type,
                                       std::string& error) {
    std::vector<int> output;
    if (!api->ready(error)) return output;
    if (type < static_cast<int>(GrenadeType::Unknown) ||
        type > static_cast<int>(GrenadeType::Inferno)) {
        error = "grenade type is out of range";
        return output;
    }
    auto* system = api->entity_system();
    if (!system) {
        error = "game entity system is unavailable";
        return output;
    }
    for (CEntityIdentity* identity =
             system->m_EntityList.m_pFirstActiveEntity;
         identity; identity = identity->m_pNext) {
        if (!identity->m_pInstance) continue;
        const char* raw = identity->GetClassname();
        const GrenadeType kind = complete_grenade_type(raw ? raw : "");
        if (kind == GrenadeType::Unknown) continue;
        if (type != static_cast<int>(GrenadeType::Unknown) &&
            type != static_cast<int>(kind)) {
            continue;
        }
        output.push_back(identity->GetEntityIndex().Get());
    }
    return output;
}

struct CreatedEntityRollback {
    const luacs::WorldServices* world{};
    int entity_index{-1};
    bool armed{true};

    ~CreatedEntityRollback() {
        if (!armed || !world || !world->entity_remove || entity_index < 0)
            return;
        char ignored[256]{};
        world->entity_remove(world->context, entity_index, ignored,
                             sizeof(ignored));
    }

    void release() { armed = false; }
};

bool grenade_spawn_complete_impl(LuaCSAdvancedApi* api,
                                 const GrenadeSpawnRequest& request,
                                 GrenadeInfo& output, std::string& error) {
    if (!api->ready(error)) return false;
    const char* classname = complete_grenade_class(request.type);
    if (!classname) {
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
    if (request.owner_entity_index >= 0 &&
        !world->entity_set_owner(world->context, created.entity_index,
                                 request.owner_entity_index, world_error,
                                 sizeof(world_error))) {
        error = world_error[0] ? world_error
                               : "grenade owner assignment failed";
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
            error = set_error.empty() ? "grenade thrower assignment failed"
                                      : set_error;
            return false;
        }
    }
    if (request.spawn &&
        !world->entity_spawn(world->context, created.entity_index,
                             world_error, sizeof(world_error))) {
        error = world_error[0] ? world_error : "grenade spawn failed";
        return false;
    }
    if (request.fuse_seconds >= 0.0f) {
        if (request.type == GrenadeType::Inferno) {
            error = "inferno is an active fire entity and does not support a detonation fuse";
            return false;
        }
        if (!world->entity_accept_input(
                world->context, created.entity_index, "Detonate", "",
                request.owner_entity_index, request.owner_entity_index,
                request.fuse_seconds, world_error, sizeof(world_error))) {
            error = world_error[0] ? world_error
                                   : "grenade rejected its Detonate input";
            return false;
        }
    }
    if (!grenade_get_complete_impl(api, created.entity_index, output, error)) {
        return false;
    }
    rollback.release();
    return true;
}

bool grenade_get_complete(void* context, int entity_index,
                          GrenadeInfo* output, char* error,
                          std::size_t error_size) {
    if (error && error_size) error[0] = '\0';
    if (!context || !output) {
        write_error(error, error_size,
                    "grenade context or output is null");
        return false;
    }
    std::string message;
    const bool result = grenade_get_complete_impl(
        static_cast<LuaCSAdvancedApi*>(context), entity_index, *output,
        message);
    if (!result) write_error(error, error_size, message);
    return result;
}

std::size_t grenade_count_complete(void* context, int type, char* error,
                                   std::size_t error_size) {
    if (error && error_size) error[0] = '\0';
    if (!context) {
        write_error(error, error_size, "grenade context is null");
        return 0;
    }
    std::string message;
    const auto values = complete_grenade_list(
        static_cast<LuaCSAdvancedApi*>(context), type, message);
    if (!message.empty()) write_error(error, error_size, message);
    return values.size();
}

bool grenade_at_complete(void* context, int type, std::size_t index,
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
    const auto values = complete_grenade_list(api, type, message);
    if (!message.empty()) {
        write_error(error, error_size, message);
        return false;
    }
    if (index >= values.size()) {
        write_error(error, error_size,
                    "grenade result index is out of range");
        return false;
    }
    if (!grenade_get_complete_impl(api, values[index], *output, message)) {
        write_error(error, error_size, message);
        return false;
    }
    return true;
}

bool grenade_spawn_complete(void* context,
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
    const bool result = grenade_spawn_complete_impl(
        static_cast<LuaCSAdvancedApi*>(context), *request, *output, message);
    if (!result) write_error(error, error_size, message);
    return result;
}

bool grenade_detonate_complete(void* context, int entity_index, char* error,
                               std::size_t error_size) {
    if (error && error_size) error[0] = '\0';
    if (!context) {
        write_error(error, error_size, "grenade context is null");
        return false;
    }
    auto* api = static_cast<LuaCSAdvancedApi*>(context);
    GrenadeInfo info;
    std::string message;
    if (!grenade_get_complete_impl(api, entity_index, info, message)) {
        write_error(error, error_size, message);
        return false;
    }
    const auto* world = LuaCS_GetWorldServices();
    char world_error[256]{};
    if (!world) {
        write_error(error, error_size, "world services are unavailable");
        return false;
    }
    if (info.type == GrenadeType::Inferno) {
        if (world->entity_accept_input &&
            world->entity_accept_input(world->context, entity_index,
                                       "Extinguish", "", entity_index,
                                       entity_index, 0.0f, world_error,
                                       sizeof(world_error))) {
            return true;
        }
        if (world->entity_remove &&
            world->entity_remove(world->context, entity_index, world_error,
                                 sizeof(world_error))) {
            return true;
        }
        write_error(error, error_size,
                    world_error[0] ? world_error
                                   : "inferno could not be extinguished or removed");
        return false;
    }
    if (!world->entity_accept_input ||
        !world->entity_accept_input(world->context, entity_index,
                                    "Detonate", "", entity_index,
                                    entity_index, 0.0f, world_error,
                                    sizeof(world_error))) {
        write_error(error, error_size,
                    world_error[0] ? world_error
                                   : "grenade rejected its Detonate input");
        return false;
    }
    return true;
}

bool grenade_remove_complete(void* context, int entity_index, char* error,
                             std::size_t error_size) {
    if (error && error_size) error[0] = '\0';
    if (!context) {
        write_error(error, error_size, "grenade context is null");
        return false;
    }
    GrenadeInfo info;
    std::string message;
    if (!grenade_get_complete_impl(
            static_cast<LuaCSAdvancedApi*>(context), entity_index, info,
            message)) {
        write_error(error, error_size, message);
        return false;
    }
    const auto* world = LuaCS_GetWorldServices();
    char world_error[256]{};
    if (!world || !world->entity_remove ||
        !world->entity_remove(world->context, entity_index, world_error,
                              sizeof(world_error))) {
        write_error(error, error_size,
                    world_error[0] ? world_error
                                   : "grenade removal service failed");
        return false;
    }
    return true;
}

struct AdvancedWorldV3Registration {
    AdvancedWorldV3Registration() {
        auto& services = g_advanced_api.services;
        services.abi_version = luacs::kAdvancedWorldServicesAbiVersion;
        services.trace = &trace_complete;
        services.grenade_get = &grenade_get_complete;
        services.grenade_count = &grenade_count_complete;
        services.grenade_at = &grenade_at_complete;
        services.grenade_spawn = &grenade_spawn_complete;
        services.grenade_detonate = &grenade_detonate_complete;
        services.grenade_remove = &grenade_remove_complete;
    }
};

AdvancedWorldV3Registration g_advanced_world_v3_registration;

} // namespace
