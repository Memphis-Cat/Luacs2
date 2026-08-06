#pragma once

#include "luacs/module_api.h"

#include <cstddef>
#include <cstdint>

namespace luacs {

inline constexpr std::uint32_t kAdvancedWorldServicesAbiVersion = 1;
inline constexpr std::size_t kPropertyNameCapacity = 192;
inline constexpr std::size_t kPropertyTypeCapacity = 96;
inline constexpr std::size_t kPropertyStringCapacity = 512;

// Values supported by the generic schema bridge. Unsupported schema types are
// reported explicitly; they are never reinterpreted as a different type.
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
};

struct TraceRequest {
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
};

struct TraceResult {
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
};

enum class GrenadeType : std::uint8_t {
    Unknown,
    HighExplosive,
    Flashbang,
    Smoke,
    Molotov,
    Incendiary,
    Decoy,
};

struct GrenadeInfo {
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
};

} // namespace luacs
