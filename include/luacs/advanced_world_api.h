#pragma once

#include "luacs/module_api.h"

#include <cstddef>
#include <cstdint>

namespace luacs {

inline constexpr std::uint32_t kAdvancedWorldServicesAbiVersion = 3;
inline constexpr std::size_t kPropertyNameCapacity = 192;
inline constexpr std::size_t kPropertyTypeCapacity = 96;
inline constexpr std::size_t kPropertyStringCapacity = 512;
inline constexpr std::size_t kPropertyRawCapacity = 4096;
inline constexpr std::size_t kTraceIgnoreCapacity = 64;
inline constexpr std::size_t kTraceMeshVertexCapacity = 256;

// Unsupported schema representations are reported explicitly. They are never
// silently reinterpreted as a different type. Raw access is available through
// the dedicated bounded raw callbacks below.
enum class PropertyKind : std::uint8_t {
    Invalid,
    Boolean,
    SignedInteger,
    UnsignedInteger,
    Float,
    String,
    Vector,
    Angle,
    EntityHandle,
    Pointer,
    Raw,
};

struct PropertyValue {
    PropertyKind kind{PropertyKind::Invalid};
    std::uint8_t width{};
    bool boolean_value{};
    std::int64_t signed_value{};
    std::uint64_t unsigned_value{};
    double float_value{};
    Vector3 vector_value{};
    int entity_index{-1};
    std::uint32_t entity_handle{0xFFFFFFFFu};
    char string_value[kPropertyStringCapacity]{};
};

struct RawPropertyValue {
    std::size_t size{};
    std::uint8_t bytes[kPropertyRawCapacity]{};
};

struct PropertyInfo {
    bool valid{};
    bool networked{};
    bool writable{};
    std::uint32_t offset{};
    std::uint16_t array_count{};
    std::uint16_t element_size{};
    PropertyKind kind{PropertyKind::Invalid};
    char name[kPropertyNameCapacity]{};
    char type_name[kPropertyTypeCapacity]{};

    // ABI v2 metadata appended after the v1 layout.
    bool readable{};
    bool fixed_array{};
    bool collection{};
    bool pointer{};
    bool embedded_class{};
    std::uint32_t byte_size{};
    int selected_index{-1};
    char owner_class[kPropertyTypeCapacity]{};
};

enum class TraceShape : std::uint8_t {
    Line = 0,
    Sphere = 1,
    Hull = 2,
    Capsule = 3,
    Mesh = 4,
};

struct TraceRequest {
    // ABI v1 fields remain first.
    Vector3 start{};
    Vector3 end{};
    Vector3 mins{};
    Vector3 maxs{};
    std::uint64_t contents_mask{0xFFFFFFFFFFFFFFFFull};
    std::uint64_t collision_group{};
    std::uint64_t ignore_entities_mask{};
    int ignore_entity_index{-1};
    bool use_hull{};
    bool hit_triggers{};

    // ABI v2 shape and filter controls.
    TraceShape shape{TraceShape::Line};
    Vector3 center_a{};
    Vector3 center_b{};
    float radius{};
    std::uint64_t contents{};
    std::uint64_t interacts_with{};
    std::uint64_t interacts_exclude{0x20311ull};
    std::uint64_t interacts_as{0x40000ull};
    bool hit_solid{true};
    bool ignore_disabled_pairs{true};
    bool ignore_if_both_hitboxes{};
    bool force_hit_everything{};
    bool iterate_entities{true};
    bool hit_entities{true};
    std::size_t ignore_count{};
    int ignore_entities[kTraceIgnoreCapacity]{};

    // ABI v3 controls. Mesh vertices are copied into this bounded request so
    // no pointer owned by a Lua module crosses the DLL boundary.
    bool hit_solid_requires_generate_contacts{};
    std::uint16_t included_detail_layers{0xFFFFu};
    std::uint8_t target_detail_layer{};
    std::size_t mesh_vertex_count{};
    Vector3 mesh_vertices[kTraceMeshVertexCapacity]{};
};

struct TraceResult {
    // ABI v1 fields remain first.
    bool valid{};
    bool hit{};
    bool start_solid{};
    bool all_solid{};
    float fraction{1.0f};
    float fraction_left_solid{};
    Vector3 start{};
    Vector3 end{};
    Vector3 hit_position{};
    Vector3 plane_normal{};
    float plane_distance{};
    int entity_index{-1};
    std::uint32_t entity_handle{0xFFFFFFFFu};
    int hitbox{-1};
    int hitgroup{};
    int surface_flags{};
    int contents{};
    char surface_name[128]{};

    // ABI v2 result data.
    float distance{};
    float hit_offset{};
    int triangle{-1};
    int bone{-1};
    bool exact_hit_point{};
    TraceShape shape{TraceShape::Line};

    // ABI v3 exposes every stable CGameTrace/shape attribute that is safe to
    // transfer across the module boundary. Source 2 has no
    // fraction-left-solid member, so availability is explicit rather than
    // fabricating a value.
    bool fraction_left_solid_available{};
    std::uint64_t contents64{};
    std::uintptr_t physics_body{};
    std::uintptr_t physics_shape{};
    std::uint64_t shape_interacts_as{};
    std::uint64_t shape_interacts_with{};
    std::uint64_t shape_interacts_exclude{};
    std::uint32_t shape_entity_id{};
    std::uint32_t shape_owner_id{0xFFFFFFFFu};
    std::uint16_t shape_hierarchy_id{};
    std::uint16_t shape_detail_layer_mask{};
    std::uint8_t shape_detail_layer_mask_type{};
    std::uint8_t shape_target_detail_layer{};
    std::uint8_t shape_collision_group{};
    std::uint8_t shape_collision_function_mask{};
};

enum class GrenadeType : std::uint8_t {
    Unknown,
    HighExplosive,
    Flashbang,
    Smoke,
    Molotov,
    Incendiary,
    Decoy,
    Inferno,
};

struct GrenadeInfo {
    // ABI v1 fields remain first.
    bool valid{};
    GrenadeType type{GrenadeType::Unknown};
    int entity_index{-1};
    std::uint32_t handle{0xFFFFFFFFu};
    int owner_entity_index{-1};
    int thrower_slot{-1};
    bool exploded{};
    bool smoke_active{};
    float spawn_time{};
    float detonate_time{};
    Vector3 position{};
    Vector3 velocity{};
    char classname[128]{};

    // ABI v2 grenade state.
    int thrower_entity_index{-1};
    int team{};
    int bounce_count{};
    int fire_count{};
    float lifetime{};
    float smoke_effect_tick{};
    bool bounce_sound{};
};

struct GrenadeSpawnRequest {
    GrenadeType type{GrenadeType::Unknown};
    Vector3 position{};
    Vector3 angles{};
    Vector3 velocity{};
    int owner_entity_index{-1};
    int thrower_slot{-1};
    float fuse_seconds{-1.0f};
    bool spawn{true};
};

struct AdvancedWorldServices {
    std::uint32_t abi_version{kAdvancedWorldServicesAbiVersion};
    void* context{};

    // ABI v1 callbacks.
    bool (*property_info)(void*, int entity_index, const char* property,
                          PropertyInfo*, char*, std::size_t){};
    bool (*property_get)(void*, int entity_index, const char* property,
                         int array_index, PropertyValue*, char*, std::size_t){};
    bool (*property_set)(void*, int entity_index, const char* property,
                         int array_index, const PropertyValue*, bool network,
                         char*, std::size_t){};
    std::size_t (*property_count)(void*, int entity_index, bool inherited,
                                  char*, std::size_t){};
    bool (*property_at)(void*, int entity_index, bool inherited,
                        std::size_t index, PropertyInfo*, char*, std::size_t){};

    bool (*trace)(void*, const TraceRequest*, TraceResult*, char*, std::size_t){};

    bool (*grenade_get)(void*, int entity_index, GrenadeInfo*, char*,
                        std::size_t){};
    std::size_t (*grenade_count)(void*, int type, char*, std::size_t){};
    bool (*grenade_at)(void*, int type, std::size_t, GrenadeInfo*, char*,
                       std::size_t){};
    bool (*grenade_spawn)(void*, const GrenadeSpawnRequest*, GrenadeInfo*,
                          char*, std::size_t){};
    bool (*grenade_detonate)(void*, int entity_index, char*, std::size_t){};
    bool (*grenade_remove)(void*, int entity_index, char*, std::size_t){};

    // ABI v2 bounded raw and dynamic-collection operations.
    bool (*property_get_raw)(void*, int entity_index, const char* property,
                             int array_index, RawPropertyValue*, PropertyInfo*,
                             char*, std::size_t){};
    bool (*property_set_raw)(void*, int entity_index, const char* property,
                             int array_index, const RawPropertyValue*,
                             bool network, char*, std::size_t){};
    std::size_t (*property_collection_count)(void*, int entity_index,
                                             const char* property, char*,
                                             std::size_t){};
    bool (*property_collection_resize)(void*, int entity_index,
                                       const char* property, std::size_t count,
                                       bool network, char*, std::size_t){};
    std::size_t (*property_child_count)(void*, int entity_index,
                                        const char* property, bool inherited,
                                        char*, std::size_t){};
    bool (*property_child_at)(void*, int entity_index, const char* property,
                              bool inherited, std::size_t index, PropertyInfo*,
                              char*, std::size_t){};
};

} // namespace luacs
