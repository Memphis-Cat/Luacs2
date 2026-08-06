#include "luacs/advanced_world_module.h"
#include "luacs/module_api.h"

extern "C" {
#include "lauxlib.h"
}

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <initializer_list>

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

int entity_index_from(lua_State* state, int index, bool allow_nil = false) {
    if (allow_nil && lua_isnoneornil(state, index)) return -1;
    if (lua_isinteger(state, index)) {
        return static_cast<int>(lua_tointeger(state, index));
    }
    luaL_checktype(state, index, LUA_TTABLE);
    lua_getfield(state, index, "entity_index");
    const int result = static_cast<int>(luaL_checkinteger(state, -1));
    lua_pop(state, 1);
    return result;
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

float float_value(const PropertyValue& value, float fallback = 0.0f) {
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
    if (first_property(api, value.entity_index,
                       {"m_hThrower", "m_hOriginalThrower"},
                       property_value)) {
        value.thrower_entity_index = integer_value(property_value, -1);
    }
    if (first_property(api, value.entity_index,
                       {"m_iTeamNum", "m_iInitialTeamNum"}, property_value)) {
        value.team = integer_value(property_value);
    }
    if (first_property(api, value.entity_index,
                       {"m_nBounces", "m_nBounceCount"}, property_value)) {
        value.bounce_count = integer_value(property_value);
    }
    if (first_property(api, value.entity_index,
                       {"m_nFireCount", "m_fireCount"}, property_value)) {
        value.fire_count = integer_value(property_value);
    }
    if (first_property(api, value.entity_index,
                       {"m_flLifetime", "m_flLifeTime"}, property_value)) {
        value.lifetime = float_value(property_value);
    } else if (value.spawn_time > 0.0f && value.detonate_time >= value.spawn_time) {
        value.lifetime = value.detonate_time - value.spawn_time;
    }
    if (first_property(api, value.entity_index,
                       {"m_nSmokeEffectTickBegin", "m_nSmokeEffectTick"},
                       property_value)) {
        value.smoke_effect_tick = float_value(property_value);
    }
    if (first_property(api, value.entity_index,
                       {"m_bBounceSound", "m_bHasBounceSound"},
                       property_value)) {
        value.bounce_sound = boolean_value(property_value);
    }
}

void push_grenade(lua_State* state, const GrenadeInfo& value) {
    lua_createtable(state, 0, 28);
#define SET_BOOL(name)                                                         \
    lua_pushboolean(state, value.name);                                        \
    lua_setfield(state, -2, #name)
#define SET_INT(name)                                                          \
    lua_pushinteger(state, value.name);                                        \
    lua_setfield(state, -2, #name)
#define SET_NUM(name)                                                          \
    lua_pushnumber(state, value.name);                                         \
    lua_setfield(state, -2, #name)
    SET_BOOL(valid);
    SET_BOOL(exploded);
    SET_BOOL(smoke_active);
    SET_BOOL(bounce_sound);
    SET_INT(entity_index);
    lua_pushinteger(state, value.handle);
    lua_setfield(state, -2, "handle");
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
    lua_setfield(state, -2, "type_id");
    lua_pushstring(state, type_name(value.type));
    lua_setfield(state, -2, "type");
    lua_pushstring(state, value.classname);
    lua_setfield(state, -2, "classname");
    push_vector(state, value.position);
    lua_setfield(state, -2, "position");
    push_vector(state, value.velocity);
    lua_setfield(state, -2, "velocity");
    luaL_getmetatable(state, kGrenadeMeta);
    lua_setmetatable(state, -2);
}

int get(lua_State* state) {
    const auto* api = advanced();
    GrenadeInfo value;
    char error[256]{};
    if (!api || !api->grenade_get ||
        !api->grenade_get(api->context, entity_index_from(state, 1), &value,
                          error, sizeof(error))) {
        return fail(state, error[0] ? error : "grenade query service is unavailable");
    }
    enrich_grenade(api, value);
    push_grenade(state, value);
    return 1;
}

int list(lua_State* state) {
    const auto* api = advanced();
    const int type = lua_isnoneornil(state, 1)
                         ? static_cast<int>(GrenadeType::Unknown)
                         : static_cast<int>(luaL_checkinteger(state, 1));
    char error[256]{};
    if (!api || !api->grenade_count || !api->grenade_at) {
        return fail(state, "grenade enumeration service is unavailable");
    }
    const std::size_t count =
        api->grenade_count(api->context, type, error, sizeof(error));
    if (error[0]) return fail(state, error);
    lua_createtable(state, static_cast<int>(count), 0);
    for (std::size_t index = 0; index < count; ++index) {
        GrenadeInfo value;
        error[0] = '\0';
        if (!api->grenade_at(api->context, type, index, &value, error,
                             sizeof(error))) {
            return fail(state, error);
        }
        enrich_grenade(api, value);
        push_grenade(state, value);
        lua_seti(state, -2, static_cast<lua_Integer>(index + 1));
    }
    return 1;
}

int spawn(lua_State* state) {
    const auto* api = advanced();
    GrenadeSpawnRequest request;
    request.type = static_cast<GrenadeType>(luaL_checkinteger(state, 1));
    request.position = read_vector(state, 2);
    request.angles = read_vector(state, 3);
    request.velocity = read_vector(state, 4);
    if (lua_istable(state, 5)) {
        lua_getfield(state, 5, "owner");
        if (!lua_isnil(state, -1)) {
            request.owner_entity_index = entity_index_from(state, -1);
        }
        lua_pop(state, 1);
        lua_getfield(state, 5, "thrower_slot");
        if (!lua_isnil(state, -1)) {
            request.thrower_slot =
                static_cast<int>(luaL_checkinteger(state, -1));
        }
        lua_pop(state, 1);
        lua_getfield(state, 5, "fuse");
        if (!lua_isnil(state, -1)) {
            request.fuse_seconds =
                static_cast<float>(luaL_checknumber(state, -1));
        }
        lua_pop(state, 1);
        lua_getfield(state, 5, "spawn");
        if (!lua_isnil(state, -1)) request.spawn = lua_toboolean(state, -1) != 0;
        lua_pop(state, 1);
    }
    GrenadeInfo output;
    char error[256]{};
    if (!api || !api->grenade_spawn ||
        !api->grenade_spawn(api->context, &request, &output, error,
                            sizeof(error))) {
        return fail(state, error[0] ? error : "grenade spawn service is unavailable");
    }
    enrich_grenade(api, output);
    push_grenade(state, output);
    return 1;
}

int detonate(lua_State* state) {
    const auto* api = advanced();
    char error[256]{};
    if (!api || !api->grenade_detonate ||
        !api->grenade_detonate(api->context, entity_index_from(state, 1), error,
                               sizeof(error))) {
        return fail(state, error[0] ? error
                                    : "grenade detonation service is unavailable");
    }
    lua_pushboolean(state, true);
    return 1;
}

int remove(lua_State* state) {
    const auto* api = advanced();
    char error[256]{};
    if (!api || !api->grenade_remove ||
        !api->grenade_remove(api->context, entity_index_from(state, 1), error,
                             sizeof(error))) {
        return fail(state, error[0] ? error : "grenade removal service is unavailable");
    }
    lua_pushboolean(state, true);
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
} // namespace

LUACS_MODULE_EXPORT int LuaCS_OpenModule(lua_State* state,
                                         const Services* api) {
    if (!api || api->abi_version != luacs::kModuleAbiVersion) {
        return luaL_error(state, "LuaCS grenades ABI mismatch");
    }
    if (!advanced()) {
        return luaL_error(state, "LuaCS advanced world services are unavailable");
    }
    if (luaL_newmetatable(state, kGrenadeMeta)) {
        lua_pushvalue(state, -1);
        lua_setfield(state, -2, "__index");
        add_function(state, api, "refresh", &get);
        add_function(state, api, "detonate", &detonate);
        add_function(state, api, "remove", &remove);
    }
    lua_pop(state, 1);

    lua_createtable(state, 0, 24);
    add_function(state, api, "get", &get);
    add_function(state, api, "list", &list);
    add_function(state, api, "all", &list);
    add_function(state, api, "spawn", &spawn);
    add_function(state, api, "detonate", &detonate);
    add_function(state, api, "remove", &remove);
    add_constant(state, "UNKNOWN", GrenadeType::Unknown);
    add_constant(state, "HE", GrenadeType::HighExplosive);
    add_constant(state, "HIGH_EXPLOSIVE", GrenadeType::HighExplosive);
    add_constant(state, "FLASHBANG", GrenadeType::Flashbang);
    add_constant(state, "SMOKE", GrenadeType::Smoke);
    add_constant(state, "MOLOTOV", GrenadeType::Molotov);
    add_constant(state, "INCENDIARY", GrenadeType::Incendiary);
    add_constant(state, "DECOY", GrenadeType::Decoy);
    add_constant(state, "INFERNO", GrenadeType::Inferno);
    return 1;
}
