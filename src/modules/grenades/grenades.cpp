#include "luacs/advanced_world_module.h"
#include "luacs/module_api.h"

extern "C" {
#include "lauxlib.h"
}

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <string>
#include <string_view>

namespace {
using luacs::AdvancedWorldServices;
using luacs::GrenadeInfo;
using luacs::GrenadeSpawnRequest;
using luacs::GrenadeType;
using luacs::PropertyKind;
using luacs::PropertyValue;
using luacs::Services;
using luacs::Vector3;

inline constexpr const char* kGrenadeMeta = "LuaCS.Grenade";

const AdvancedWorldServices* advanced() {
    return luacs::resolve_advanced_world_services();
}

int fail(lua_State* state, const char* error) {
    lua_pushnil(state);
    lua_pushstring(state, error && *error ? error : "grenade operation failed");
    return 2;
}

bool finite_vector(const Vector3& value) {
    return std::isfinite(value.x) && std::isfinite(value.y) &&
           std::isfinite(value.z);
}

bool integer_field(lua_State* state, int table, const char* field,
                   lua_Integer& value) {
    table = lua_absindex(state, table);
    lua_getfield(state, table, field);
    if (!lua_isinteger(state, -1)) {
        lua_pop(state, 1);
        return false;
    }
    value = lua_tointeger(state, -1);
    lua_pop(state, 1);
    return true;
}

int checked_entity_index(lua_State* state, int argument, lua_Integer raw,
                         bool allow_negative = false) {
    if ((!allow_negative && raw < 0) || raw < -1 ||
        raw > std::numeric_limits<int>::max()) {
        return luaL_argerror(state, argument,
                             "entity index is outside the supported range");
    }
    return static_cast<int>(raw);
}

int entity_index_from(lua_State* state, int index, bool allow_nil = false) {
    if (allow_nil && lua_isnoneornil(state, index)) return -1;
    if (lua_isinteger(state, index)) {
        return checked_entity_index(state, index, lua_tointeger(state, index),
                                    allow_nil);
    }
    luaL_checktype(state, index, LUA_TTABLE);
    lua_Integer value{};
    if (integer_field(state, index, "entity_index", value)) {
        return checked_entity_index(state, index, value, allow_nil);
    }
    if (integer_field(state, index, "pawn_index", value) && value >= 0) {
        return checked_entity_index(state, index, value, false);
    }
    if (integer_field(state, index, "controller_index", value)) {
        return checked_entity_index(state, index, value, false);
    }
    return luaL_argerror(
        state, index,
        "expected an entity/index, weapon, grenade, handle, or player table");
}

int player_slot_from(lua_State* state, int index) {
    if (lua_isinteger(state, index)) {
        const lua_Integer slot = lua_tointeger(state, index);
        if (slot < 0 || slot >= 64) {
            return luaL_argerror(state, index,
                                 "player slot must be between 0 and 63");
        }
        return static_cast<int>(slot);
    }
    luaL_checktype(state, index, LUA_TTABLE);
    lua_Integer slot{};
    if (!integer_field(state, index, "slot", slot) || slot < 0 || slot >= 64) {
        return luaL_argerror(state, index,
                             "player table does not contain a valid slot");
    }
    return static_cast<int>(slot);
}

Vector3 read_vector(lua_State* state, int index) {
    luaL_checktype(state, index, LUA_TTABLE);
    Vector3 value;
    lua_getfield(state, index, "x");
    value.x = static_cast<float>(luaL_checknumber(state, -1));
    lua_pop(state, 1);
    lua_getfield(state, index, "y");
    value.y = static_cast<float>(luaL_checknumber(state, -1));
    lua_pop(state, 1);
    lua_getfield(state, index, "z");
    value.z = static_cast<float>(luaL_checknumber(state, -1));
    lua_pop(state, 1);
    if (!finite_vector(value)) {
        luaL_argerror(state, index, "grenade vector components must be finite");
    }
    return value;
}

void push_vector(lua_State* state, const Vector3& value) {
    lua_createtable(state, 0, 4);
    lua_pushnumber(state, value.x);
    lua_setfield(state, -2, "x");
    lua_pushnumber(state, value.y);
    lua_setfield(state, -2, "y");
    lua_pushnumber(state, value.z);
    lua_setfield(state, -2, "z");
    lua_pushliteral(state, "Vector");
    lua_setfield(state, -2, "__type");
}

const char* type_name(GrenadeType type) {
    switch (type) {
        case GrenadeType::HighExplosive: return "he";
        case GrenadeType::Flashbang: return "flashbang";
        case GrenadeType::Smoke: return "smoke";
        case GrenadeType::Molotov: return "molotov";
        case GrenadeType::Incendiary: return "incendiary";
        case GrenadeType::Decoy: return "decoy";
        case GrenadeType::Inferno: return "inferno";
        default: return "unknown";
    }
}

std::string lower_ascii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](char ch) {
        if (ch >= 'A' && ch <= 'Z') return static_cast<char>(ch - 'A' + 'a');
        return ch;
    });
    return value;
}

GrenadeType parse_type(lua_State* state, int index, bool allow_unknown) {
    if (lua_isinteger(state, index)) {
        const lua_Integer raw = lua_tointeger(state, index);
        const lua_Integer minimum = allow_unknown
                                        ? static_cast<lua_Integer>(
                                              GrenadeType::Unknown)
                                        : static_cast<lua_Integer>(
                                              GrenadeType::HighExplosive);
        if (raw < minimum ||
            raw > static_cast<lua_Integer>(GrenadeType::Inferno)) {
            luaL_argerror(state, index, "grenade type is out of range");
        }
        return static_cast<GrenadeType>(raw);
    }

    const std::string name = lower_ascii(luaL_checkstring(state, index));
    if (allow_unknown && (name == "unknown" || name == "all" || name == "*"))
        return GrenadeType::Unknown;
    if (name == "he" || name == "hegrenade" || name == "high_explosive")
        return GrenadeType::HighExplosive;
    if (name == "flash" || name == "flashbang") return GrenadeType::Flashbang;
    if (name == "smoke" || name == "smokegrenade") return GrenadeType::Smoke;
    if (name == "molotov") return GrenadeType::Molotov;
    if (name == "inc" || name == "incendiary" || name == "incgrenade")
        return GrenadeType::Incendiary;
    if (name == "decoy") return GrenadeType::Decoy;
    if (name == "inferno" || name == "fire") return GrenadeType::Inferno;
    luaL_argerror(state, index,
                  "grenade type must be he, flashbang, smoke, molotov, incendiary, decoy, or inferno");
    return GrenadeType::Unknown;
}

bool property(const AdvancedWorldServices* api, int entity,
              const char* path, PropertyValue& output) {
    if (!api || !api->property_get) return false;
    char error[256]{};
    return api->property_get(api->context, entity, path, -1, &output, error,
                             sizeof(error));
}

bool first_property(const AdvancedWorldServices* api, int entity,
                    std::initializer_list<const char*> paths,
                    PropertyValue& output) {
    for (const char* path : paths) {
        if (property(api, entity, path, output)) return true;
    }
    return false;
}

int integer_value(const PropertyValue& value, int fallback = 0) {
    switch (value.kind) {
        case PropertyKind::SignedInteger:
            if (value.signed_value < std::numeric_limits<int>::min() ||
                value.signed_value > std::numeric_limits<int>::max()) {
                return fallback;
            }
            return static_cast<int>(value.signed_value);
        case PropertyKind::UnsignedInteger:
        case PropertyKind::Pointer:
            if (value.unsigned_value >
                static_cast<std::uint64_t>(std::numeric_limits<int>::max())) {
                return fallback;
            }
            return static_cast<int>(value.unsigned_value);
        case PropertyKind::EntityHandle:
            return value.entity_index;
        case PropertyKind::Float:
            if (!std::isfinite(value.float_value) ||
                value.float_value < std::numeric_limits<int>::min() ||
                value.float_value > std::numeric_limits<int>::max()) {
                return fallback;
            }
            return static_cast<int>(value.float_value);
        default:
            return fallback;
    }
}

float float_value(const PropertyValue& value, float fallback = 0.0f) {
    switch (value.kind) {
        case PropertyKind::Float:
            return std::isfinite(value.float_value)
                       ? static_cast<float>(value.float_value)
                       : fallback;
        case PropertyKind::SignedInteger:
            return static_cast<float>(value.signed_value);
        case PropertyKind::UnsignedInteger:
            return static_cast<float>(value.unsigned_value);
        default:
            return fallback;
    }
}

bool boolean_value(const PropertyValue& value, bool fallback = false) {
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

void enrich_grenade(const AdvancedWorldServices* api, GrenadeInfo& value) {
    if (!value.valid || value.entity_index < 0) return;

    PropertyValue property_value;
    if (value.thrower_entity_index < 0 &&
        first_property(api, value.entity_index,
                       {"m_hThrower", "m_hOriginalThrower"},
                       property_value)) {
        value.thrower_entity_index = integer_value(property_value, -1);
    }
    if (first_property(api, value.entity_index,
                       {"m_iTeamNum", "m_iInitialTeamNum"}, property_value)) {
        value.team = integer_value(property_value, value.team);
    }
    if (first_property(api, value.entity_index,
                       {"m_nBounces", "m_nBounceCount"}, property_value)) {
        value.bounce_count = integer_value(property_value, value.bounce_count);
    }
    if (first_property(api, value.entity_index,
                       {"m_nFireCount", "m_fireCount"}, property_value)) {
        value.fire_count = integer_value(property_value, value.fire_count);
    }
    if (first_property(api, value.entity_index,
                       {"m_flLifetime", "m_flLifeTime"}, property_value)) {
        value.lifetime = float_value(property_value, value.lifetime);
    } else if (value.spawn_time > 0.0f &&
               value.detonate_time >= value.spawn_time) {
        value.lifetime = value.detonate_time - value.spawn_time;
    }
    if (first_property(api, value.entity_index,
                       {"m_nSmokeEffectTickBegin", "m_nSmokeEffectTick"},
                       property_value)) {
        value.smoke_effect_tick =
            float_value(property_value, value.smoke_effect_tick);
    }
    if (first_property(api, value.entity_index,
                       {"m_bBounceSound", "m_bHasBounceSound"},
                       property_value)) {
        value.bounce_sound =
            boolean_value(property_value, value.bounce_sound);
    }
}

bool valid_grenade_info(const GrenadeInfo& value) {
    return value.valid && value.entity_index >= 0 &&
           value.type >= GrenadeType::HighExplosive &&
           value.type <= GrenadeType::Inferno && finite_vector(value.position) &&
           finite_vector(value.velocity) && std::isfinite(value.spawn_time) &&
           std::isfinite(value.detonate_time) && std::isfinite(value.lifetime) &&
           std::isfinite(value.smoke_effect_tick);
}

void apply_grenade(lua_State* state, int table, const GrenadeInfo& value) {
    table = lua_absindex(state, table);
#define SET_BOOL(name)                                                         \
    lua_pushboolean(state, value.name);                                        \
    lua_setfield(state, table, #name)
#define SET_INT(name)                                                          \
    lua_pushinteger(state, value.name);                                        \
    lua_setfield(state, table, #name)
#define SET_NUM(name)                                                          \
    lua_pushnumber(state, value.name);                                         \
    lua_setfield(state, table, #name)
    SET_BOOL(valid);
    SET_BOOL(exploded);
    SET_BOOL(smoke_active);
    SET_BOOL(bounce_sound);
    SET_INT(entity_index);
    lua_pushinteger(state, value.handle);
    lua_setfield(state, table, "handle");
    SET_INT(owner_entity_index);
    SET_INT(thrower_slot);
    SET_INT(thrower_entity_index);
    SET_INT(team);
    SET_INT(bounce_count);
    SET_INT(fire_count);
    SET_NUM(spawn_time);
    SET_NUM(detonate_time);
    SET_NUM(lifetime);
    SET_NUM(smoke_effect_tick);
#undef SET_NUM
#undef SET_INT
#undef SET_BOOL
    lua_pushinteger(state, static_cast<int>(value.type));
    lua_setfield(state, table, "type_id");
    lua_pushstring(state, type_name(value.type));
    lua_setfield(state, table, "type");
    lua_pushstring(state, value.classname);
    lua_setfield(state, table, "classname");
    push_vector(state, value.position);
    lua_setfield(state, table, "position");
    push_vector(state, value.velocity);
    lua_setfield(state, table, "velocity");
    lua_pushboolean(state, value.type != GrenadeType::Inferno);
    lua_setfield(state, table, "projectile");
    lua_pushboolean(state, value.type == GrenadeType::Inferno);
    lua_setfield(state, table, "effect");
}

void push_grenade(lua_State* state, const GrenadeInfo& value) {
    lua_createtable(state, 0, 32);
    apply_grenade(state, -1, value);
    luaL_getmetatable(state, kGrenadeMeta);
    lua_setmetatable(state, -2);
}

bool query_grenade(int entity, GrenadeInfo& value, char* error,
                   std::size_t error_size) {
    const auto* api = advanced();
    if (!api || !api->grenade_get ||
        !api->grenade_get(api->context, entity, &value, error, error_size)) {
        return false;
    }
    enrich_grenade(api, value);
    if (!valid_grenade_info(value)) {
        if (error && error_size) {
            std::snprintf(error, error_size, "%s",
                          "Source 2 returned invalid grenade state");
        }
        return false;
    }
    return true;
}

int get(lua_State* state) {
    GrenadeInfo value;
    char error[512]{};
    if (!query_grenade(entity_index_from(state, 1), value, error,
                       sizeof(error))) {
        return fail(state,
                    error[0] ? error : "grenade query service is unavailable");
    }
    push_grenade(state, value);
    return 1;
}

int refresh(lua_State* state) {
    luaL_checktype(state, 1, LUA_TTABLE);
    GrenadeInfo value;
    char error[512]{};
    if (!query_grenade(entity_index_from(state, 1), value, error,
                       sizeof(error))) {
        return fail(state,
                    error[0] ? error : "grenade query service is unavailable");
    }
    apply_grenade(state, 1, value);
    lua_pushvalue(state, 1);
    return 1;
}

int is_valid(lua_State* state) {
    GrenadeInfo value;
    char error[512]{};
    const bool valid = query_grenade(entity_index_from(state, 1), value, error,
                                     sizeof(error));
    lua_pushboolean(state, valid);
    return 1;
}

GrenadeType optional_type(lua_State* state, int index) {
    return lua_isnoneornil(state, index)
               ? GrenadeType::Unknown
               : parse_type(state, index, true);
}

int count(lua_State* state) {
    const auto* api = advanced();
    const int type = static_cast<int>(optional_type(state, 1));
    char error[512]{};
    if (!api || !api->grenade_count) {
        return fail(state, "grenade count service is unavailable");
    }
    const std::size_t total =
        api->grenade_count(api->context, type, error, sizeof(error));
    if (error[0]) return fail(state, error);
    lua_pushinteger(state, static_cast<lua_Integer>(total));
    return 1;
}

int list(lua_State* state) {
    const auto* api = advanced();
    const int type = static_cast<int>(optional_type(state, 1));
    char error[512]{};
    if (!api || !api->grenade_count || !api->grenade_at) {
        return fail(state, "grenade enumeration service is unavailable");
    }
    const std::size_t total =
        api->grenade_count(api->context, type, error, sizeof(error));
    if (error[0]) return fail(state, error);
    if (total > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        return fail(state, "grenade result is too large for a Lua table");
    }
    lua_createtable(state, static_cast<int>(total), 0);
    for (std::size_t index = 0; index < total; ++index) {
        GrenadeInfo value;
        error[0] = '\0';
        if (!api->grenade_at(api->context, type, index, &value, error,
                             sizeof(error))) {
            return fail(state, error);
        }
        enrich_grenade(api, value);
        if (!valid_grenade_info(value)) {
            return fail(state, "Source 2 returned invalid grenade state");
        }
        push_grenade(state, value);
        lua_seti(state, -2, static_cast<lua_Integer>(index + 1));
    }
    return 1;
}

void read_spawn_options(lua_State* state, int table,
                        GrenadeSpawnRequest& request) {
    if (lua_isnoneornil(state, table)) return;
    luaL_checktype(state, table, LUA_TTABLE);
    table = lua_absindex(state, table);

    lua_getfield(state, table, "owner");
    if (!lua_isnil(state, -1)) {
        request.owner_entity_index = entity_index_from(state, -1);
    }
    lua_pop(state, 1);

    lua_getfield(state, table, "thrower");
    if (!lua_isnil(state, -1)) {
        request.thrower_slot = player_slot_from(state, -1);
    }
    lua_pop(state, 1);

    lua_getfield(state, table, "thrower_slot");
    if (!lua_isnil(state, -1)) {
        const int slot = player_slot_from(state, -1);
        if (request.thrower_slot >= 0 && request.thrower_slot != slot) {
            luaL_error(state,
                       "thrower and thrower_slot identify different players");
        }
        request.thrower_slot = slot;
    }
    lua_pop(state, 1);

    lua_getfield(state, table, "fuse");
    if (!lua_isnil(state, -1)) {
        request.fuse_seconds = static_cast<float>(luaL_checknumber(state, -1));
        if (!std::isfinite(request.fuse_seconds) ||
            request.fuse_seconds < -1.0f) {
            luaL_error(state, "grenade fuse must be -1 or non-negative and finite");
        }
    }
    lua_pop(state, 1);

    lua_getfield(state, table, "spawn");
    if (!lua_isnil(state, -1)) {
        luaL_checktype(state, -1, LUA_TBOOLEAN);
        request.spawn = lua_toboolean(state, -1) != 0;
    }
    lua_pop(state, 1);
}

int spawn_impl(lua_State* state, GrenadeType forced_type, int base) {
    const auto* api = advanced();
    GrenadeSpawnRequest request;
    request.type = forced_type == GrenadeType::Unknown
                       ? parse_type(state, base, false)
                       : forced_type;
    const int vector_base = forced_type == GrenadeType::Unknown ? base + 1 : base;
    request.position = read_vector(state, vector_base);
    request.angles = read_vector(state, vector_base + 1);
    request.velocity = read_vector(state, vector_base + 2);
    read_spawn_options(state, vector_base + 3, request);

    GrenadeInfo output;
    char error[512]{};
    if (!api || !api->grenade_spawn ||
        !api->grenade_spawn(api->context, &request, &output, error,
                            sizeof(error))) {
        return fail(state,
                    error[0] ? error : "grenade spawn service is unavailable");
    }
    enrich_grenade(api, output);
    if (!valid_grenade_info(output)) {
        return fail(state, "Source 2 returned invalid spawned grenade state");
    }
    push_grenade(state, output);
    return 1;
}

int spawn(lua_State* state) {
    return spawn_impl(state, GrenadeType::Unknown, 1);
}
int spawn_he(lua_State* state) {
    return spawn_impl(state, GrenadeType::HighExplosive, 1);
}
int spawn_flashbang(lua_State* state) {
    return spawn_impl(state, GrenadeType::Flashbang, 1);
}
int spawn_smoke(lua_State* state) {
    return spawn_impl(state, GrenadeType::Smoke, 1);
}
int spawn_molotov(lua_State* state) {
    return spawn_impl(state, GrenadeType::Molotov, 1);
}
int spawn_incendiary(lua_State* state) {
    return spawn_impl(state, GrenadeType::Incendiary, 1);
}
int spawn_decoy(lua_State* state) {
    return spawn_impl(state, GrenadeType::Decoy, 1);
}
int spawn_inferno(lua_State* state) {
    return spawn_impl(state, GrenadeType::Inferno, 1);
}

int detonate(lua_State* state) {
    const auto* api = advanced();
    const int entity = entity_index_from(state, 1);
    char error[512]{};
    if (!api || !api->grenade_detonate ||
        !api->grenade_detonate(api->context, entity, error, sizeof(error))) {
        return fail(state,
                    error[0] ? error
                             : "grenade detonation service is unavailable");
    }
    lua_pushboolean(state, true);
    return 1;
}

int remove(lua_State* state) {
    const auto* api = advanced();
    const int entity = entity_index_from(state, 1);
    char error[512]{};
    if (!api || !api->grenade_remove ||
        !api->grenade_remove(api->context, entity, error, sizeof(error))) {
        return fail(state,
                    error[0] ? error : "grenade removal service is unavailable");
    }
    if (lua_istable(state, 1)) {
        lua_pushboolean(state, false);
        lua_setfield(state, 1, "valid");
    }
    lua_pushboolean(state, true);
    return 1;
}

int filter_by_owner(lua_State* state) {
    const int owner = entity_index_from(state, 1);
    const GrenadeType type = optional_type(state, 2);
    const auto* api = advanced();
    char error[512]{};
    if (!api || !api->grenade_count || !api->grenade_at) {
        return fail(state, "grenade enumeration service is unavailable");
    }
    const int type_id = static_cast<int>(type);
    const std::size_t total =
        api->grenade_count(api->context, type_id, error, sizeof(error));
    if (error[0]) return fail(state, error);
    lua_createtable(state, 0, 0);
    lua_Integer next = 1;
    for (std::size_t index = 0; index < total; ++index) {
        GrenadeInfo value;
        error[0] = '\0';
        if (!api->grenade_at(api->context, type_id, index, &value, error,
                             sizeof(error))) {
            return fail(state, error);
        }
        enrich_grenade(api, value);
        if (value.owner_entity_index != owner &&
            value.thrower_entity_index != owner) {
            continue;
        }
        push_grenade(state, value);
        lua_seti(state, -2, next++);
    }
    return 1;
}

int filter_by_thrower(lua_State* state) {
    const int slot = player_slot_from(state, 1);
    const GrenadeType type = optional_type(state, 2);
    const auto* api = advanced();
    char error[512]{};
    if (!api || !api->grenade_count || !api->grenade_at) {
        return fail(state, "grenade enumeration service is unavailable");
    }
    const int type_id = static_cast<int>(type);
    const std::size_t total =
        api->grenade_count(api->context, type_id, error, sizeof(error));
    if (error[0]) return fail(state, error);
    lua_createtable(state, 0, 0);
    lua_Integer next = 1;
    for (std::size_t index = 0; index < total; ++index) {
        GrenadeInfo value;
        error[0] = '\0';
        if (!api->grenade_at(api->context, type_id, index, &value, error,
                             sizeof(error))) {
            return fail(state, error);
        }
        enrich_grenade(api, value);
        if (value.thrower_slot != slot) continue;
        push_grenade(state, value);
        lua_seti(state, -2, next++);
    }
    return 1;
}

int type_name_fn(lua_State* state) {
    const GrenadeType type = parse_type(state, 1, true);
    lua_pushstring(state, type_name(type));
    return 1;
}

int type_id_fn(lua_State* state) {
    const GrenadeType type = parse_type(state, 1, true);
    lua_pushinteger(state, static_cast<int>(type));
    return 1;
}

int grenade_fuse_duration(lua_State* state) {
    luaL_checktype(state, 1, LUA_TTABLE);
    lua_getfield(state, 1, "spawn_time");
    const lua_Number spawn_time = luaL_checknumber(state, -1);
    lua_pop(state, 1);
    lua_getfield(state, 1, "detonate_time");
    const lua_Number detonate_time = luaL_checknumber(state, -1);
    lua_pop(state, 1);
    if (!std::isfinite(spawn_time) || !std::isfinite(detonate_time) ||
        detonate_time < spawn_time) {
        lua_pushnil(state);
        return 1;
    }
    lua_pushnumber(state, detonate_time - spawn_time);
    return 1;
}

int grenade_tostring(lua_State* state) {
    luaL_checktype(state, 1, LUA_TTABLE);
    lua_getfield(state, 1, "type");
    const char* type = luaL_checkstring(state, -1);
    lua_pop(state, 1);
    lua_getfield(state, 1, "entity_index");
    const int entity = static_cast<int>(luaL_checkinteger(state, -1));
    lua_pop(state, 1);
    lua_pushfstring(state, "Grenade(%s, entity=%d)", type, entity);
    return 1;
}

void add_function(lua_State* state, const Services* api, const char* name,
                  lua_CFunction function) {
    lua_pushlightuserdata(state, const_cast<Services*>(api));
    lua_pushcclosure(state, function, 1);
    lua_setfield(state, -2, name);
}

void add_constant(lua_State* state, const char* name, GrenadeType value) {
    lua_pushinteger(state, static_cast<int>(value));
    lua_setfield(state, -2, name);
}

void add_type_map(lua_State* state) {
    lua_createtable(state, 0, 8);
    add_constant(state, "unknown", GrenadeType::Unknown);
    add_constant(state, "he", GrenadeType::HighExplosive);
    add_constant(state, "flashbang", GrenadeType::Flashbang);
    add_constant(state, "smoke", GrenadeType::Smoke);
    add_constant(state, "molotov", GrenadeType::Molotov);
    add_constant(state, "incendiary", GrenadeType::Incendiary);
    add_constant(state, "decoy", GrenadeType::Decoy);
    add_constant(state, "inferno", GrenadeType::Inferno);
    lua_setfield(state, -2, "types");
}
} // namespace

LUACS_MODULE_EXPORT int LuaCS_OpenModule(lua_State* state,
                                         const Services* api) {
    if (!api || api->abi_version != luacs::kModuleAbiVersion) {
        return luaL_error(state, "LuaCS grenades ABI mismatch");
    }
    if (!advanced()) {
        return luaL_error(state,
                          "LuaCS advanced world services are unavailable");
    }

    if (luaL_newmetatable(state, kGrenadeMeta)) {
        lua_newtable(state);
        add_function(state, api, "refresh", &refresh);
        add_function(state, api, "is_valid", &is_valid);
        add_function(state, api, "detonate", &detonate);
        add_function(state, api, "remove", &remove);
        add_function(state, api, "fuse_duration", &grenade_fuse_duration);
        lua_setfield(state, -2, "__index");
        add_function(state, api, "__tostring", &grenade_tostring);
    }
    lua_pop(state, 1);

    lua_createtable(state, 0, 48);
    add_function(state, api, "get", &get);
    add_function(state, api, "is_valid", &is_valid);
    add_function(state, api, "count", &count);
    add_function(state, api, "list", &list);
    add_function(state, api, "all", &list);
    add_function(state, api, "by_owner", &filter_by_owner);
    add_function(state, api, "by_thrower", &filter_by_thrower);
    add_function(state, api, "spawn", &spawn);
    add_function(state, api, "create", &spawn);
    add_function(state, api, "spawn_he", &spawn_he);
    add_function(state, api, "spawn_flashbang", &spawn_flashbang);
    add_function(state, api, "spawn_smoke", &spawn_smoke);
    add_function(state, api, "spawn_molotov", &spawn_molotov);
    add_function(state, api, "spawn_incendiary", &spawn_incendiary);
    add_function(state, api, "spawn_decoy", &spawn_decoy);
    add_function(state, api, "spawn_inferno", &spawn_inferno);
    add_function(state, api, "detonate", &detonate);
    add_function(state, api, "remove", &remove);
    add_function(state, api, "type_name", &type_name_fn);
    add_function(state, api, "type_id", &type_id_fn);

    add_constant(state, "UNKNOWN", GrenadeType::Unknown);
    add_constant(state, "HE", GrenadeType::HighExplosive);
    add_constant(state, "HIGH_EXPLOSIVE", GrenadeType::HighExplosive);
    add_constant(state, "FLASHBANG", GrenadeType::Flashbang);
    add_constant(state, "SMOKE", GrenadeType::Smoke);
    add_constant(state, "MOLOTOV", GrenadeType::Molotov);
    add_constant(state, "INCENDIARY", GrenadeType::Incendiary);
    add_constant(state, "DECOY", GrenadeType::Decoy);
    add_constant(state, "INFERNO", GrenadeType::Inferno);
    add_type_map(state);
    return 1;
}
