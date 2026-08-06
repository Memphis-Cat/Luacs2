#include "luacs/advanced_world_module.h"
#include "luacs/module_api.h"

extern "C" {
#include "lauxlib.h"
}

#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace {

using luacs::AdvancedWorldServices;
using luacs::PropertyInfo;
using luacs::PropertyKind;
using luacs::PropertyValue;
using luacs::Services;
using luacs::Vector3;

const Services* services(lua_State* state) {
    return static_cast<const Services*>(
        lua_touserdata(state, lua_upvalueindex(1)));
}

const AdvancedWorldServices* advanced() {
    return luacs::resolve_advanced_world_services();
}

int fail(lua_State* state, const char* error) {
    lua_pushnil(state);
    lua_pushstring(state,
                   error && *error ? error : "entity property operation failed");
    return 2;
}

int entity_index_from(lua_State* state, int index) {
    if (lua_isinteger(state, index)) {
        return static_cast<int>(lua_tointeger(state, index));
    }
    luaL_checktype(state, index, LUA_TTABLE);
    lua_getfield(state, index, "entity_index");
    const int value = static_cast<int>(luaL_checkinteger(state, -1));
    lua_pop(state, 1);
    return value;
}

void push_vector(lua_State* state, const Vector3& value,
                 const char* type_name = "Vector") {
    lua_createtable(state, 0, 4);
    lua_pushnumber(state, value.x);
    lua_setfield(state, -2, "x");
    lua_pushnumber(state, value.y);
    lua_setfield(state, -2, "y");
    lua_pushnumber(state, value.z);
    lua_setfield(state, -2, "z");
    lua_pushstring(state, type_name);
    lua_setfield(state, -2, "__type");
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

const char* kind_name(PropertyKind kind) {
    switch (kind) {
        case PropertyKind::Boolean: return "boolean";
        case PropertyKind::SignedInteger: return "integer";
        case PropertyKind::UnsignedInteger: return "unsigned_integer";
        case PropertyKind::Float: return "float";
        case PropertyKind::String: return "string";
        case PropertyKind::Vector: return "vector";
        case PropertyKind::Angle: return "angle";
        case PropertyKind::EntityHandle: return "entity_handle";
        case PropertyKind::Pointer: return "pointer";
        default: return "invalid";
    }
}

void push_info(lua_State* state, const PropertyInfo& value) {
    lua_createtable(state, 0, 11);
    lua_pushboolean(state, value.valid);
    lua_setfield(state, -2, "valid");
    lua_pushboolean(state, value.networked);
    lua_setfield(state, -2, "networked");
    lua_pushboolean(state, value.writable);
    lua_setfield(state, -2, "writable");
    lua_pushinteger(state, value.offset);
    lua_setfield(state, -2, "offset");
    lua_pushinteger(state, value.array_count);
    lua_setfield(state, -2, "array_count");
    lua_pushinteger(state, value.element_size);
    lua_setfield(state, -2, "element_size");
    lua_pushinteger(state, static_cast<int>(value.kind));
    lua_setfield(state, -2, "kind_id");
    lua_pushstring(state, kind_name(value.kind));
    lua_setfield(state, -2, "kind");
    lua_pushstring(state, value.name);
    lua_setfield(state, -2, "name");
    lua_pushstring(state, value.type_name);
    lua_setfield(state, -2, "type_name");
}

void push_value(lua_State* state, const PropertyValue& value) {
    switch (value.kind) {
        case PropertyKind::Boolean:
            lua_pushboolean(state, value.boolean_value);
            break;
        case PropertyKind::SignedInteger:
            lua_pushinteger(state, static_cast<lua_Integer>(value.signed_value));
            break;
        case PropertyKind::UnsignedInteger:
        case PropertyKind::Pointer:
            lua_pushinteger(state,
                            static_cast<lua_Integer>(value.unsigned_value));
            break;
        case PropertyKind::Float:
            lua_pushnumber(state, value.float_value);
            break;
        case PropertyKind::String:
            lua_pushstring(state, value.string_value);
            break;
        case PropertyKind::Vector:
            push_vector(state, value.vector_value, "Vector");
            break;
        case PropertyKind::Angle:
            push_vector(state, value.vector_value, "QAngle");
            break;
        case PropertyKind::EntityHandle:
            lua_createtable(state, 0, 3);
            lua_pushinteger(state, value.entity_index);
            lua_setfield(state, -2, "entity_index");
            lua_pushinteger(state, value.entity_handle);
            lua_setfield(state, -2, "handle");
            lua_pushliteral(state, "EntityHandle");
            lua_setfield(state, -2, "__type");
            break;
        default:
            lua_pushnil(state);
            break;
    }
}

PropertyValue read_value(lua_State* state, int index, PropertyKind kind) {
    PropertyValue output;
    output.kind = kind;
    switch (kind) {
        case PropertyKind::Boolean:
            output.boolean_value = lua_toboolean(state, index) != 0;
            break;
        case PropertyKind::SignedInteger:
            output.signed_value =
                static_cast<std::int64_t>(luaL_checkinteger(state, index));
            break;
        case PropertyKind::UnsignedInteger:
        case PropertyKind::Pointer:
            output.unsigned_value =
                static_cast<std::uint64_t>(luaL_checkinteger(state, index));
            break;
        case PropertyKind::Float:
            output.float_value = luaL_checknumber(state, index);
            break;
        case PropertyKind::String: {
            const char* value = luaL_checkstring(state, index);
            std::snprintf(output.string_value, sizeof(output.string_value), "%s",
                          value);
            break;
        }
        case PropertyKind::Vector:
        case PropertyKind::Angle:
            output.vector_value = read_vector(state, index);
            break;
        case PropertyKind::EntityHandle:
            if (lua_isnil(state, index)) {
                output.entity_index = -1;
                output.entity_handle = 0xFFFFFFFFu;
            } else if (lua_isinteger(state, index)) {
                output.entity_index =
                    static_cast<int>(lua_tointeger(state, index));
            } else {
                output.entity_index = entity_index_from(state, index);
            }
            break;
        default:
            luaL_error(state, "property type is not writable");
    }
    return output;
}

int info(lua_State* state) {
    const auto* api = advanced();
    const int entity = entity_index_from(state, 1);
    const char* property = luaL_checkstring(state, 2);
    PropertyInfo result;
    char error[256]{};
    if (!api || !api->property_info ||
        !api->property_info(api->context, entity, property, &result, error,
                            sizeof(error))) {
        return fail(state, error[0] ? error
                                    : "property metadata service is unavailable");
    }
    push_info(state, result);
    return 1;
}

int get(lua_State* state) {
    const auto* api = advanced();
    const int entity = entity_index_from(state, 1);
    const char* property = luaL_checkstring(state, 2);
    const int array_index =
        lua_isnoneornil(state, 3)
            ? -1
            : static_cast<int>(luaL_checkinteger(state, 3));
    PropertyValue result;
    char error[256]{};
    if (!api || !api->property_get ||
        !api->property_get(api->context, entity, property, array_index, &result,
                           error, sizeof(error))) {
        return fail(state, error[0] ? error : "property read service is unavailable");
    }
    push_value(state, result);
    return 1;
}

int set(lua_State* state) {
    const auto* api = advanced();
    const int entity = entity_index_from(state, 1);
    const char* property = luaL_checkstring(state, 2);
    PropertyInfo metadata;
    char error[256]{};
    if (!api || !api->property_info || !api->property_set ||
        !api->property_info(api->context, entity, property, &metadata, error,
                            sizeof(error))) {
        return fail(state, error[0] ? error : "property write service is unavailable");
    }
    const int array_index =
        lua_isnoneornil(state, 4)
            ? -1
            : static_cast<int>(luaL_checkinteger(state, 4));
    const bool network = lua_isnoneornil(state, 5) || lua_toboolean(state, 5);
    const PropertyValue value = read_value(state, 3, metadata.kind);
    error[0] = '\0';
    if (!api->property_set(api->context, entity, property, array_index, &value,
                           network, error, sizeof(error))) {
        return fail(state, error);
    }
    lua_pushboolean(state, true);
    return 1;
}

int list(lua_State* state) {
    const auto* api = advanced();
    const int entity = entity_index_from(state, 1);
    const bool inherited = lua_isnoneornil(state, 2) || lua_toboolean(state, 2);
    char error[256]{};
    if (!api || !api->property_count || !api->property_at) {
        return fail(state, "property enumeration service is unavailable");
    }
    const std::size_t count = api->property_count(
        api->context, entity, inherited, error, sizeof(error));
    if (error[0]) return fail(state, error);
    lua_createtable(state, static_cast<int>(count), 0);
    for (std::size_t index = 0; index < count; ++index) {
        PropertyInfo value;
        error[0] = '\0';
        if (!api->property_at(api->context, entity, inherited, index, &value,
                              error, sizeof(error))) {
            return fail(state, error);
        }
        push_info(state, value);
        lua_seti(state, -2, static_cast<lua_Integer>(index + 1));
    }
    return 1;
}

void add_function(lua_State* state, const Services* api, const char* name,
                  lua_CFunction function) {
    lua_pushlightuserdata(state, const_cast<Services*>(api));
    lua_pushcclosure(state, function, 1);
    lua_setfield(state, -2, name);
}

void add_constant(lua_State* state, const char* name, PropertyKind value) {
    lua_pushinteger(state, static_cast<int>(value));
    lua_setfield(state, -2, name);
}

} // namespace

LUACS_MODULE_EXPORT int LuaCS_OpenModule(lua_State* state,
                                         const Services* api) {
    if (!api || api->abi_version != luacs::kModuleAbiVersion) {
        return luaL_error(state, "LuaCS properties ABI mismatch");
    }
    if (!advanced()) {
        return luaL_error(state, "LuaCS advanced world services are unavailable");
    }

    lua_createtable(state, 0, 16);
    add_function(state, api, "info", &info);
    add_function(state, api, "get", &get);
    add_function(state, api, "set", &set);
    add_function(state, api, "list", &list);
    add_constant(state, "BOOLEAN", PropertyKind::Boolean);
    add_constant(state, "SIGNED_INTEGER", PropertyKind::SignedInteger);
    add_constant(state, "UNSIGNED_INTEGER", PropertyKind::UnsignedInteger);
    add_constant(state, "FLOAT", PropertyKind::Float);
    add_constant(state, "STRING", PropertyKind::String);
    add_constant(state, "VECTOR", PropertyKind::Vector);
    add_constant(state, "ANGLE", PropertyKind::Angle);
    add_constant(state, "ENTITY_HANDLE", PropertyKind::EntityHandle);
    add_constant(state, "POINTER", PropertyKind::Pointer);
    return 1;
}
