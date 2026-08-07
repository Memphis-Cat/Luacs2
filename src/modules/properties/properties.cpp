#include "luacs/advanced_world_module.h"
#include "luacs/module_api.h"

extern "C" {
#include "lauxlib.h"
}

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

namespace {

using luacs::AdvancedWorldServices;
using luacs::PropertyInfo;
using luacs::PropertyKind;
using luacs::PropertyValue;
using luacs::RawPropertyValue;
using luacs::Services;
using luacs::Vector3;

inline constexpr const char* kPropertyRefMeta = "LuaCS.PropertyRef";

const AdvancedWorldServices* advanced() {
    return luacs::resolve_advanced_world_services();
}

int fail(lua_State* state, const char* error) {
    lua_pushnil(state);
    lua_pushstring(state,
                   error && *error ? error : "entity property operation failed");
    return 2;
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

int entity_index_from(lua_State* state, int index) {
    if (lua_isinteger(state, index)) {
        return static_cast<int>(lua_tointeger(state, index));
    }
    luaL_checktype(state, index, LUA_TTABLE);

    lua_Integer value{};
    if (integer_field(state, index, "entity_index", value)) {
        return static_cast<int>(value);
    }
    if (integer_field(state, index, "pawn_index", value) && value >= 0) {
        return static_cast<int>(value);
    }
    if (integer_field(state, index, "controller_index", value)) {
        return static_cast<int>(value);
    }
    return luaL_argerror(
        state, index,
        "expected an entity/index, weapon, handle, or player table with a live pawn/controller");
}

int optional_array_index(lua_State* state, int index) {
    if (lua_isnoneornil(state, index)) return -1;
    const lua_Integer value = luaL_checkinteger(state, index);
    if (value < 0 || value > std::numeric_limits<int>::max()) {
        luaL_argerror(state, index,
                      "array index must be between 0 and INT_MAX");
        return -1;
    }
    return static_cast<int>(value);
}

bool optional_network(lua_State* state, int index) {
    return lua_isnoneornil(state, index) || lua_toboolean(state, index) != 0;
}

std::string indexed_path(lua_State* state, int argument,
                         std::string_view path, int index) {
    if (index < 0) return std::string(path);
    const std::size_t segment = path.rfind('.');
    const std::size_t bracket = path.find(
        '[', segment == std::string_view::npos ? 0 : segment + 1);
    if (bracket != std::string_view::npos) {
        luaL_argerror(state, argument,
                      "final property segment already contains an array index");
        return {};
    }
    std::string result(path);
    result.push_back('[');
    result += std::to_string(index);
    result.push_back(']');
    return result;
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
    if (!std::isfinite(value.x) || !std::isfinite(value.y) ||
        !std::isfinite(value.z)) {
        luaL_argerror(state, index, "vector components must be finite");
    }
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
        case PropertyKind::Raw: return "raw";
        default: return "invalid";
    }
}

bool typed_directly(const PropertyInfo& value) {
    if (!value.readable || value.collection || value.kind == PropertyKind::Raw ||
        value.kind == PropertyKind::Invalid) {
        return false;
    }
    if (value.fixed_array && value.selected_index < 0 &&
        value.kind != PropertyKind::String) {
        return false;
    }
    return true;
}

void apply_info(lua_State* state, int table, const PropertyInfo& value) {
    table = lua_absindex(state, table);
    lua_pushboolean(state, value.valid);
    lua_setfield(state, table, "valid");
    lua_pushboolean(state, value.networked);
    lua_setfield(state, table, "networked");
    lua_pushboolean(state, value.writable);
    lua_setfield(state, table, "writable");
    lua_pushboolean(state, value.readable);
    lua_setfield(state, table, "readable");
    lua_pushinteger(state, value.offset);
    lua_setfield(state, table, "offset");
    lua_pushinteger(state, value.array_count);
    lua_setfield(state, table, "array_count");
    lua_pushinteger(state, value.element_size);
    lua_setfield(state, table, "element_size");
    lua_pushinteger(state, static_cast<int>(value.kind));
    lua_setfield(state, table, "kind_id");
    lua_pushstring(state, kind_name(value.kind));
    lua_setfield(state, table, "kind");
    lua_pushstring(state, kind_name(value.kind));
    lua_setfield(state, table, "kind_name");
    lua_pushstring(state, value.name);
    lua_setfield(state, table, "name");
    lua_pushstring(state, value.name);
    lua_setfield(state, table, "path");
    lua_pushstring(state, value.type_name);
    lua_setfield(state, table, "type_name");
    lua_pushboolean(state, value.fixed_array);
    lua_setfield(state, table, "fixed_array");
    lua_pushboolean(state, value.collection);
    lua_setfield(state, table, "collection");
    lua_pushboolean(state, value.pointer);
    lua_setfield(state, table, "pointer");
    lua_pushboolean(state, value.embedded_class);
    lua_setfield(state, table, "embedded_class");
    lua_pushinteger(state, value.byte_size);
    lua_setfield(state, table, "byte_size");
    lua_pushinteger(state, value.selected_index);
    lua_setfield(state, table, "selected_index");
    lua_pushstring(state, value.owner_class);
    lua_setfield(state, table, "owner_class");
    lua_pushboolean(state, value.fixed_array || value.collection);
    lua_setfield(state, table, "indexable");
    lua_pushboolean(state,
                    value.fixed_array || value.collection ||
                        value.embedded_class);
    lua_setfield(state, table, "aggregate");
    lua_pushboolean(state, typed_directly(value));
    lua_setfield(state, table, "typed_readable");
    lua_pushboolean(state, typed_directly(value) && value.writable);
    lua_setfield(state, table, "typed_writable");
    lua_pushboolean(state, value.byte_size != 0 && !value.collection);
    lua_setfield(state, table, "raw_readable");
    lua_pushboolean(state, value.byte_size != 0 && !value.collection &&
                                   value.writable);
    lua_setfield(state, table, "raw_writable");
}

void push_info(lua_State* state, const PropertyInfo& value) {
    lua_createtable(state, 0, 30);
    apply_info(state, -1, value);
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
            if (value.unsigned_value >
                static_cast<std::uint64_t>(std::numeric_limits<lua_Integer>::max())) {
                lua_pushnumber(state, static_cast<lua_Number>(value.unsigned_value));
            } else {
                lua_pushinteger(
                    state, static_cast<lua_Integer>(value.unsigned_value));
            }
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
            lua_createtable(state, 0, 5);
            lua_pushboolean(state, value.entity_index >= 0);
            lua_setfield(state, -2, "valid");
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
            luaL_checktype(state, index, LUA_TBOOLEAN);
            output.boolean_value = lua_toboolean(state, index) != 0;
            break;
        case PropertyKind::SignedInteger:
            output.signed_value =
                static_cast<std::int64_t>(luaL_checkinteger(state, index));
            break;
        case PropertyKind::UnsignedInteger: {
            const lua_Integer value = luaL_checkinteger(state, index);
            if (value < 0) {
                luaL_argerror(state, index,
                              "unsigned schema value cannot be negative");
            }
            output.unsigned_value = static_cast<std::uint64_t>(value);
            break;
        }
        case PropertyKind::Pointer: {
            const lua_Integer value = luaL_checkinteger(state, index);
            if (value < 0) {
                luaL_argerror(state, index,
                              "pointer schema value cannot be negative");
            }
            output.unsigned_value = static_cast<std::uint64_t>(value);
            break;
        }
        case PropertyKind::Float:
            output.float_value = luaL_checknumber(state, index);
            if (!std::isfinite(output.float_value)) {
                luaL_argerror(state, index,
                              "floating-point schema value must be finite");
            }
            break;
        case PropertyKind::String: {
            std::size_t length{};
            const char* value = luaL_checklstring(state, index, &length);
            if (length >= luacs::kPropertyStringCapacity) {
                luaL_error(state,
                           "schema string exceeds the %d-byte property value capacity",
                           static_cast<int>(luacs::kPropertyStringCapacity - 1));
            }
            if (length) std::memcpy(output.string_value, value, length);
            output.string_value[length] = '\0';
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
                const lua_Integer entity = lua_tointeger(state, index);
                if (entity < 0 || entity > std::numeric_limits<int>::max()) {
                    luaL_argerror(state, index,
                                  "entity handle target index is out of range");
                }
                output.entity_index = static_cast<int>(entity);
            } else {
                output.entity_index = entity_index_from(state, index);
            }
            break;
        default:
            luaL_error(state,
                       "property type requires properties.get_raw/set_raw");
    }
    return output;
}

bool fetch_info(const AdvancedWorldServices* api, int entity,
                std::string_view property, int array_index,
                PropertyInfo& result, char* error, std::size_t error_size) {
    if (!api || !api->property_info) {
        if (error && error_size) {
            std::snprintf(error, error_size, "%s",
                          "property metadata service is unavailable");
        }
        return false;
    }
    std::string path(property);
    if (array_index >= 0) {
        const std::size_t segment = property.rfind('.');
        const std::size_t bracket = property.find(
            '[', segment == std::string_view::npos ? 0 : segment + 1);
        if (bracket != std::string_view::npos) {
            if (error && error_size) {
                std::snprintf(
                    error, error_size, "%s",
                    "final property segment already contains an array index");
            }
            return false;
        }
        path.push_back('[');
        path += std::to_string(array_index);
        path.push_back(']');
    }
    return api->property_info(api->context, entity, path.c_str(), &result,
                              error, error_size);
}

int info(lua_State* state) {
    const auto* api = advanced();
    const int entity = entity_index_from(state, 1);
    const char* property = luaL_checkstring(state, 2);
    const int array_index = optional_array_index(state, 3);
    PropertyInfo result;
    char error[512]{};
    if (!fetch_info(api, entity, property, array_index, result, error,
                    sizeof(error))) {
        return fail(state, error);
    }
    push_info(state, result);
    return 1;
}

int exists(lua_State* state) {
    const auto* api = advanced();
    const int entity = entity_index_from(state, 1);
    const char* property = luaL_checkstring(state, 2);
    const int array_index = optional_array_index(state, 3);
    PropertyInfo result;
    char error[512]{};
    const bool found = fetch_info(api, entity, property, array_index, result,
                                  error, sizeof(error));
    lua_pushboolean(state, found);
    return 1;
}

int kind(lua_State* state) {
    const auto* api = advanced();
    const int entity = entity_index_from(state, 1);
    const char* property = luaL_checkstring(state, 2);
    const int array_index = optional_array_index(state, 3);
    PropertyInfo result;
    char error[512]{};
    if (!fetch_info(api, entity, property, array_index, result, error,
                    sizeof(error))) {
        return fail(state, error);
    }
    lua_pushstring(state, kind_name(result.kind));
    lua_pushinteger(state, static_cast<int>(result.kind));
    return 2;
}

int get(lua_State* state) {
    const auto* api = advanced();
    const int entity = entity_index_from(state, 1);
    const char* property = luaL_checkstring(state, 2);
    const int array_index = optional_array_index(state, 3);
    PropertyValue result;
    char error[512]{};
    if (!api || !api->property_get ||
        !api->property_get(api->context, entity, property, array_index, &result,
                           error, sizeof(error))) {
        return fail(state,
                    error[0] ? error : "property read service is unavailable");
    }
    push_value(state, result);
    return 1;
}

int get_typed(lua_State* state, PropertyKind expected, const char* label) {
    const auto* api = advanced();
    const int entity = entity_index_from(state, 1);
    const char* property = luaL_checkstring(state, 2);
    const int array_index = optional_array_index(state, 3);
    PropertyValue result;
    char error[512]{};
    if (!api || !api->property_get ||
        !api->property_get(api->context, entity, property, array_index, &result,
                           error, sizeof(error))) {
        return fail(state,
                    error[0] ? error : "property read service is unavailable");
    }
    if (result.kind != expected) {
        std::snprintf(error, sizeof(error),
                      "schema property is %s, not %s",
                      kind_name(result.kind), label);
        return fail(state, error);
    }
    push_value(state, result);
    return 1;
}

int get_boolean(lua_State* state) {
    return get_typed(state, PropertyKind::Boolean, "boolean");
}
int get_integer(lua_State* state) {
    return get_typed(state, PropertyKind::SignedInteger, "integer");
}
int get_unsigned(lua_State* state) {
    return get_typed(state, PropertyKind::UnsignedInteger,
                     "unsigned_integer");
}
int get_float(lua_State* state) {
    return get_typed(state, PropertyKind::Float, "float");
}
int get_string(lua_State* state) {
    return get_typed(state, PropertyKind::String, "string");
}
int get_vector(lua_State* state) {
    return get_typed(state, PropertyKind::Vector, "vector");
}
int get_angle(lua_State* state) {
    return get_typed(state, PropertyKind::Angle, "angle");
}
int get_handle(lua_State* state) {
    return get_typed(state, PropertyKind::EntityHandle, "entity_handle");
}
int get_pointer(lua_State* state) {
    return get_typed(state, PropertyKind::Pointer, "pointer");
}

int set_common(lua_State* state, PropertyKind forced_kind,
               bool force_kind) {
    const auto* api = advanced();
    const int entity = entity_index_from(state, 1);
    const char* property = luaL_checkstring(state, 2);
    const int array_index = optional_array_index(state, 4);
    const bool network = optional_network(state, 5);
    PropertyInfo metadata;
    char error[512]{};
    if (!fetch_info(api, entity, property, array_index, metadata, error,
                    sizeof(error))) {
        return fail(state, error);
    }
    if (force_kind && metadata.kind != forced_kind) {
        std::snprintf(error, sizeof(error),
                      "schema property is %s, not %s",
                      kind_name(metadata.kind), kind_name(forced_kind));
        return fail(state, error);
    }
    const PropertyKind value_kind = force_kind ? forced_kind : metadata.kind;
    const PropertyValue value = read_value(state, 3, value_kind);
    error[0] = '\0';
    if (!api || !api->property_set ||
        !api->property_set(api->context, entity, property, array_index, &value,
                           network, error, sizeof(error))) {
        return fail(state,
                    error[0] ? error : "property write service is unavailable");
    }
    lua_pushboolean(state, true);
    return 1;
}

int set(lua_State* state) {
    return set_common(state, PropertyKind::Invalid, false);
}
int set_boolean(lua_State* state) {
    return set_common(state, PropertyKind::Boolean, true);
}
int set_integer(lua_State* state) {
    return set_common(state, PropertyKind::SignedInteger, true);
}
int set_unsigned(lua_State* state) {
    return set_common(state, PropertyKind::UnsignedInteger, true);
}
int set_float(lua_State* state) {
    return set_common(state, PropertyKind::Float, true);
}
int set_string(lua_State* state) {
    return set_common(state, PropertyKind::String, true);
}
int set_vector(lua_State* state) {
    return set_common(state, PropertyKind::Vector, true);
}
int set_angle(lua_State* state) {
    return set_common(state, PropertyKind::Angle, true);
}
int set_handle(lua_State* state) {
    return set_common(state, PropertyKind::EntityHandle, true);
}
int set_pointer(lua_State* state) {
    return set_common(state, PropertyKind::Pointer, true);
}

int get_raw(lua_State* state) {
    const auto* api = advanced();
    const int entity = entity_index_from(state, 1);
    const char* property = luaL_checkstring(state, 2);
    const int array_index = optional_array_index(state, 3);
    RawPropertyValue result;
    PropertyInfo metadata;
    char error[512]{};
    if (!api || !api->property_get_raw ||
        !api->property_get_raw(api->context, entity, property, array_index,
                               &result, &metadata, error, sizeof(error))) {
        return fail(state,
                    error[0] ? error
                             : "raw property read service is unavailable");
    }
    lua_pushlstring(state, reinterpret_cast<const char*>(result.bytes),
                    result.size);
    push_info(state, metadata);
    return 2;
}

int set_raw(lua_State* state) {
    const auto* api = advanced();
    const int entity = entity_index_from(state, 1);
    const char* property = luaL_checkstring(state, 2);
    std::size_t size{};
    const char* bytes = luaL_checklstring(state, 3, &size);
    if (size > luacs::kPropertyRawCapacity) {
        return luaL_error(state, "raw property value exceeds %d bytes",
                          static_cast<int>(luacs::kPropertyRawCapacity));
    }
    const int array_index = optional_array_index(state, 4);
    const bool network = optional_network(state, 5);
    RawPropertyValue input;
    input.size = size;
    if (size) std::memcpy(input.bytes, bytes, size);
    char error[512]{};
    if (!api || !api->property_set_raw ||
        !api->property_set_raw(api->context, entity, property, array_index,
                               &input, network, error, sizeof(error))) {
        return fail(state,
                    error[0] ? error
                             : "raw property write service is unavailable");
    }
    lua_pushboolean(state, true);
    return 1;
}

bool property_count_value(const AdvancedWorldServices* api, int entity,
                          const char* property, std::size_t& output,
                          PropertyInfo& metadata, char* error,
                          std::size_t error_size) {
    if (!fetch_info(api, entity, property, -1, metadata, error, error_size)) {
        return false;
    }
    if (metadata.fixed_array) {
        output = metadata.array_count;
        return true;
    }
    if (metadata.collection) {
        if (!api->property_collection_count) {
            if (error && error_size) {
                std::snprintf(error, error_size, "%s",
                              "collection count service is unavailable");
            }
            return false;
        }
        output = api->property_collection_count(api->context, entity, property,
                                                error, error_size);
        return !error || !error_size || error[0] == '\0';
    }
    if (error && error_size) {
        std::snprintf(error, error_size, "%s",
                      "schema property is not a fixed array or dynamic collection");
    }
    return false;
}

int count(lua_State* state) {
    const auto* api = advanced();
    const int entity = entity_index_from(state, 1);
    const char* property = luaL_checkstring(state, 2);
    std::size_t total{};
    PropertyInfo metadata;
    char error[512]{};
    if (!property_count_value(api, entity, property, total, metadata, error,
                              sizeof(error))) {
        return fail(state, error);
    }
    lua_pushinteger(state, static_cast<lua_Integer>(total));
    return 1;
}

int collection_count(lua_State* state) {
    const auto* api = advanced();
    const int entity = entity_index_from(state, 1);
    const char* property = luaL_checkstring(state, 2);
    char error[512]{};
    if (!api || !api->property_collection_count) {
        return fail(state, "collection count service is unavailable");
    }
    const std::size_t total = api->property_collection_count(
        api->context, entity, property, error, sizeof(error));
    if (error[0]) return fail(state, error);
    lua_pushinteger(state, static_cast<lua_Integer>(total));
    return 1;
}

int collection_resize(lua_State* state) {
    const auto* api = advanced();
    const int entity = entity_index_from(state, 1);
    const char* property = luaL_checkstring(state, 2);
    const lua_Integer requested = luaL_checkinteger(state, 3);
    if (requested < 0 || requested > std::numeric_limits<int>::max()) {
        return luaL_error(state,
                          "collection size must be between 0 and INT_MAX");
    }
    const bool network = optional_network(state, 4);
    char error[512]{};
    if (!api || !api->property_collection_resize ||
        !api->property_collection_resize(
            api->context, entity, property, static_cast<std::size_t>(requested),
            network, error, sizeof(error))) {
        return fail(state,
                    error[0] ? error
                             : "collection resize service is unavailable");
    }
    lua_pushboolean(state, true);
    return 1;
}

int values(lua_State* state) {
    const auto* api = advanced();
    const int entity = entity_index_from(state, 1);
    const char* property = luaL_checkstring(state, 2);
    std::size_t total{};
    PropertyInfo metadata;
    char error[512]{};
    if (!property_count_value(api, entity, property, total, metadata, error,
                              sizeof(error))) {
        return fail(state, error);
    }
    if (!api || !api->property_get) {
        return fail(state, "property read service is unavailable");
    }
    if (total > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        return fail(state, "schema collection is too large for a Lua table");
    }
    lua_createtable(state, static_cast<int>(total), 0);
    for (std::size_t index = 0; index < total; ++index) {
        PropertyValue value;
        error[0] = '\0';
        if (!api->property_get(api->context, entity, property,
                               static_cast<int>(index), &value, error,
                               sizeof(error))) {
            return fail(state, error);
        }
        push_value(state, value);
        lua_seti(state, -2, static_cast<lua_Integer>(index + 1));
    }
    return 1;
}

int list(lua_State* state) {
    const auto* api = advanced();
    const int entity = entity_index_from(state, 1);
    const bool inherited = optional_network(state, 2);
    char error[512]{};
    if (!api || !api->property_count || !api->property_at) {
        return fail(state, "property enumeration service is unavailable");
    }
    const std::size_t total = api->property_count(
        api->context, entity, inherited, error, sizeof(error));
    if (error[0]) return fail(state, error);
    if (total > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        return fail(state, "schema property list is too large for a Lua table");
    }
    lua_createtable(state, static_cast<int>(total), 0);
    for (std::size_t index = 0; index < total; ++index) {
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

int children(lua_State* state) {
    const auto* api = advanced();
    const int entity = entity_index_from(state, 1);
    const char* property = lua_isnoneornil(state, 2)
                               ? ""
                               : luaL_checkstring(state, 2);
    const bool inherited = optional_network(state, 3);
    char error[512]{};
    if (!api || !api->property_child_count || !api->property_child_at) {
        return fail(state, "child property enumeration service is unavailable");
    }
    const std::size_t total = api->property_child_count(
        api->context, entity, property, inherited, error, sizeof(error));
    if (error[0]) return fail(state, error);
    if (total > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        return fail(state, "schema child list is too large for a Lua table");
    }
    lua_createtable(state, static_cast<int>(total), 0);
    for (std::size_t index = 0; index < total; ++index) {
        PropertyInfo value;
        error[0] = '\0';
        if (!api->property_child_at(api->context, entity, property, inherited,
                                    index, &value, error, sizeof(error))) {
            return fail(state, error);
        }
        push_info(state, value);
        lua_seti(state, -2, static_cast<lua_Integer>(index + 1));
    }
    return 1;
}

bool append_walk(lua_State* state, const AdvancedWorldServices* api,
                 int entity, const std::string& parent, bool inherited,
                 int depth, int max_depth, int table, lua_Integer& next,
                 std::string& failure) {
    char error[512]{};
    const std::size_t total = api->property_child_count(
        api->context, entity, parent.c_str(), inherited, error, sizeof(error));
    if (error[0]) {
        failure = error;
        return false;
    }
    for (std::size_t index = 0; index < total; ++index) {
        PropertyInfo value;
        error[0] = '\0';
        if (!api->property_child_at(api->context, entity, parent.c_str(),
                                    inherited, index, &value, error,
                                    sizeof(error))) {
            failure = error;
            return false;
        }
        push_info(state, value);
        lua_seti(state, table, next++);
        if (depth < max_depth && (value.embedded_class || value.pointer)) {
            if (!append_walk(state, api, entity, value.name, inherited,
                             depth + 1, max_depth, table, next, failure)) {
                return false;
            }
        }
    }
    return true;
}

int walk(lua_State* state) {
    const auto* api = advanced();
    const int entity = entity_index_from(state, 1);
    const char* root = lua_isnoneornil(state, 2) ? "" : luaL_checkstring(state, 2);
    const bool inherited = optional_network(state, 3);
    const lua_Integer requested_depth = lua_isnoneornil(state, 4)
                                            ? 4
                                            : luaL_checkinteger(state, 4);
    if (requested_depth < 0 || requested_depth > 16) {
        return luaL_argerror(state, 4,
                             "walk depth must be between 0 and 16");
    }
    if (!api || !api->property_child_count || !api->property_child_at) {
        return fail(state, "child property enumeration service is unavailable");
    }
    lua_createtable(state, 0, 0);
    const int table = lua_absindex(state, -1);
    lua_Integer next = 1;
    std::string failure;
    if (!append_walk(state, api, entity, root, inherited, 0,
                     static_cast<int>(requested_depth), table, next,
                     failure)) {
        lua_pop(state, 1);
        return fail(state, failure.c_str());
    }
    return 1;
}

int ref_entity(lua_State* state) {
    lua_getfield(state, 1, "entity_index");
    const int entity = static_cast<int>(luaL_checkinteger(state, -1));
    lua_pop(state, 1);
    return entity;
}

std::string ref_property(lua_State* state) {
    lua_getfield(state, 1, "property");
    const char* value = luaL_checkstring(state, -1);
    std::string result(value);
    lua_pop(state, 1);
    return result;
}

int ref_index(lua_State* state) {
    lua_getfield(state, 1, "array_index");
    const int index = static_cast<int>(luaL_checkinteger(state, -1));
    lua_pop(state, 1);
    return index;
}

bool refresh_ref_table(lua_State* state, int table, char* error,
                       std::size_t error_size) {
    const auto* api = advanced();
    const int entity = ref_entity(state);
    const std::string property = ref_property(state);
    const int array_index = ref_index(state);
    PropertyInfo metadata;
    if (!fetch_info(api, entity, property, array_index, metadata, error,
                    error_size)) {
        return false;
    }
    apply_info(state, table, metadata);
    return true;
}

int property_ref(lua_State* state) {
    const int entity = entity_index_from(state, 1);
    const char* property = luaL_checkstring(state, 2);
    const int array_index = optional_array_index(state, 3);
    lua_createtable(state, 0, 36);
    const int table = lua_absindex(state, -1);
    lua_pushinteger(state, entity);
    lua_setfield(state, table, "entity_index");
    lua_pushstring(state, property);
    lua_setfield(state, table, "property");
    lua_pushinteger(state, array_index);
    lua_setfield(state, table, "array_index");
    char error[512]{};
    if (!refresh_ref_table(state, table, error, sizeof(error))) {
        lua_pop(state, 1);
        return fail(state, error);
    }
    luaL_getmetatable(state, kPropertyRefMeta);
    lua_setmetatable(state, table);
    return 1;
}

int ref_refresh(lua_State* state) {
    luaL_checktype(state, 1, LUA_TTABLE);
    char error[512]{};
    if (!refresh_ref_table(state, 1, error, sizeof(error))) {
        return fail(state, error);
    }
    lua_pushvalue(state, 1);
    return 1;
}

int ref_get(lua_State* state) {
    const auto* api = advanced();
    const int entity = ref_entity(state);
    const std::string property = ref_property(state);
    const int array_index = ref_index(state);
    PropertyValue result;
    char error[512]{};
    if (!api || !api->property_get ||
        !api->property_get(api->context, entity, property.c_str(), array_index,
                           &result, error, sizeof(error))) {
        return fail(state, error[0] ? error : "property read service is unavailable");
    }
    push_value(state, result);
    return 1;
}

int ref_set(lua_State* state) {
    const auto* api = advanced();
    const int entity = ref_entity(state);
    const std::string property = ref_property(state);
    const int array_index = ref_index(state);
    const bool network = optional_network(state, 3);
    PropertyInfo metadata;
    char error[512]{};
    if (!fetch_info(api, entity, property, array_index, metadata, error,
                    sizeof(error))) {
        return fail(state, error);
    }
    const PropertyValue value = read_value(state, 2, metadata.kind);
    error[0] = '\0';
    if (!api || !api->property_set ||
        !api->property_set(api->context, entity, property.c_str(), array_index,
                           &value, network, error, sizeof(error))) {
        return fail(state, error[0] ? error : "property write service is unavailable");
    }
    lua_pushboolean(state, true);
    return 1;
}

int ref_get_raw(lua_State* state) {
    const auto* api = advanced();
    const int entity = ref_entity(state);
    const std::string property = ref_property(state);
    const int array_index = ref_index(state);
    RawPropertyValue value;
    PropertyInfo metadata;
    char error[512]{};
    if (!api || !api->property_get_raw ||
        !api->property_get_raw(api->context, entity, property.c_str(),
                               array_index, &value, &metadata, error,
                               sizeof(error))) {
        return fail(state, error[0] ? error : "raw property read service is unavailable");
    }
    lua_pushlstring(state, reinterpret_cast<const char*>(value.bytes),
                    value.size);
    push_info(state, metadata);
    return 2;
}

int ref_set_raw(lua_State* state) {
    const auto* api = advanced();
    const int entity = ref_entity(state);
    const std::string property = ref_property(state);
    const int array_index = ref_index(state);
    std::size_t size{};
    const char* bytes = luaL_checklstring(state, 2, &size);
    if (size > luacs::kPropertyRawCapacity) {
        return luaL_error(state, "raw property value exceeds %d bytes",
                          static_cast<int>(luacs::kPropertyRawCapacity));
    }
    const bool network = optional_network(state, 3);
    RawPropertyValue value;
    value.size = size;
    if (size) std::memcpy(value.bytes, bytes, size);
    char error[512]{};
    if (!api || !api->property_set_raw ||
        !api->property_set_raw(api->context, entity, property.c_str(),
                               array_index, &value, network, error,
                               sizeof(error))) {
        return fail(state, error[0] ? error : "raw property write service is unavailable");
    }
    lua_pushboolean(state, true);
    return 1;
}

int ref_count(lua_State* state) {
    const auto* api = advanced();
    const int entity = ref_entity(state);
    const std::string property = ref_property(state);
    if (ref_index(state) >= 0) {
        return fail(state, "indexed property reference is already an element");
    }
    std::size_t total{};
    PropertyInfo metadata;
    char error[512]{};
    if (!property_count_value(api, entity, property.c_str(), total, metadata,
                              error, sizeof(error))) {
        return fail(state, error);
    }
    lua_pushinteger(state, static_cast<lua_Integer>(total));
    return 1;
}

int ref_values(lua_State* state) {
    const auto* api = advanced();
    const int entity = ref_entity(state);
    const std::string property = ref_property(state);
    if (ref_index(state) >= 0) {
        return fail(state, "indexed property reference is already an element");
    }
    std::size_t total{};
    PropertyInfo metadata;
    char error[512]{};
    if (!property_count_value(api, entity, property.c_str(), total, metadata,
                              error, sizeof(error))) {
        return fail(state, error);
    }
    if (!api || !api->property_get) {
        return fail(state, "property read service is unavailable");
    }
    lua_createtable(state, static_cast<int>(total), 0);
    for (std::size_t index = 0; index < total; ++index) {
        PropertyValue value;
        error[0] = '\0';
        if (!api->property_get(api->context, entity, property.c_str(),
                               static_cast<int>(index), &value, error,
                               sizeof(error))) {
            return fail(state, error);
        }
        push_value(state, value);
        lua_seti(state, -2, static_cast<lua_Integer>(index + 1));
    }
    return 1;
}

int ref_resize(lua_State* state) {
    const auto* api = advanced();
    const int entity = ref_entity(state);
    const std::string property = ref_property(state);
    if (ref_index(state) >= 0) {
        return fail(state, "indexed property reference is already an element");
    }
    const lua_Integer requested = luaL_checkinteger(state, 2);
    if (requested < 0 || requested > std::numeric_limits<int>::max()) {
        return luaL_argerror(state, 2,
                             "collection size must be between 0 and INT_MAX");
    }
    const bool network = optional_network(state, 3);
    char error[512]{};
    if (!api || !api->property_collection_resize ||
        !api->property_collection_resize(
            api->context, entity, property.c_str(),
            static_cast<std::size_t>(requested), network, error,
            sizeof(error))) {
        return fail(state, error[0] ? error : "collection resize service is unavailable");
    }
    lua_pushboolean(state, true);
    return 1;
}

int ref_children(lua_State* state) {
    const auto* api = advanced();
    const int entity = ref_entity(state);
    const std::string property = ref_property(state);
    const int array_index = ref_index(state);
    const bool inherited = optional_network(state, 2);
    std::string path = property;
    if (array_index >= 0) {
        const std::size_t segment = path.rfind('.');
        const std::size_t bracket = path.find(
            '[', segment == std::string::npos ? 0 : segment + 1);
        if (bracket != std::string::npos) {
            return fail(state,
                        "final property segment already contains an array index");
        }
        path.push_back('[');
        path += std::to_string(array_index);
        path.push_back(']');
    }
    char error[512]{};
    if (!api || !api->property_child_count || !api->property_child_at) {
        return fail(state, "child property enumeration service is unavailable");
    }
    const std::size_t total = api->property_child_count(
        api->context, entity, path.c_str(), inherited, error, sizeof(error));
    if (error[0]) return fail(state, error);
    lua_createtable(state, static_cast<int>(total), 0);
    for (std::size_t index = 0; index < total; ++index) {
        PropertyInfo value;
        error[0] = '\0';
        if (!api->property_child_at(api->context, entity, path.c_str(),
                                    inherited, index, &value, error,
                                    sizeof(error))) {
            return fail(state, error);
        }
        push_info(state, value);
        lua_seti(state, -2, static_cast<lua_Integer>(index + 1));
    }
    return 1;
}

int ref_tostring(lua_State* state) {
    const int entity = ref_entity(state);
    const std::string property = ref_property(state);
    const int array_index = ref_index(state);
    if (array_index >= 0) {
        lua_pushfstring(state, "PropertyRef(%d, %s[%d])", entity,
                        property.c_str(), array_index);
    } else {
        lua_pushfstring(state, "PropertyRef(%d, %s)", entity,
                        property.c_str());
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

void add_kind_map(lua_State* state) {
    lua_createtable(state, 0, 10);
    add_constant(state, "boolean", PropertyKind::Boolean);
    add_constant(state, "integer", PropertyKind::SignedInteger);
    add_constant(state, "unsigned_integer", PropertyKind::UnsignedInteger);
    add_constant(state, "float", PropertyKind::Float);
    add_constant(state, "string", PropertyKind::String);
    add_constant(state, "vector", PropertyKind::Vector);
    add_constant(state, "angle", PropertyKind::Angle);
    add_constant(state, "entity_handle", PropertyKind::EntityHandle);
    add_constant(state, "pointer", PropertyKind::Pointer);
    add_constant(state, "raw", PropertyKind::Raw);
    lua_setfield(state, -2, "kinds");
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

    if (luaL_newmetatable(state, kPropertyRefMeta)) {
        lua_newtable(state);
        add_function(state, api, "refresh", &ref_refresh);
        add_function(state, api, "get", &ref_get);
        add_function(state, api, "set", &ref_set);
        add_function(state, api, "get_raw", &ref_get_raw);
        add_function(state, api, "set_raw", &ref_set_raw);
        add_function(state, api, "count", &ref_count);
        add_function(state, api, "values", &ref_values);
        add_function(state, api, "resize", &ref_resize);
        add_function(state, api, "children", &ref_children);
        lua_setfield(state, -2, "__index");
        add_function(state, api, "__tostring", &ref_tostring);
    }
    lua_pop(state, 1);

    lua_createtable(state, 0, 52);
    add_function(state, api, "info", &info);
    add_function(state, api, "exists", &exists);
    add_function(state, api, "kind", &kind);
    add_function(state, api, "get", &get);
    add_function(state, api, "set", &set);
    add_function(state, api, "get_raw", &get_raw);
    add_function(state, api, "set_raw", &set_raw);
    add_function(state, api, "count", &count);
    add_function(state, api, "values", &values);
    add_function(state, api, "get_all", &values);
    add_function(state, api, "collection_count", &collection_count);
    add_function(state, api, "collection_resize", &collection_resize);
    add_function(state, api, "list", &list);
    add_function(state, api, "children", &children);
    add_function(state, api, "walk", &walk);
    add_function(state, api, "ref", &property_ref);

    add_function(state, api, "get_boolean", &get_boolean);
    add_function(state, api, "get_bool", &get_boolean);
    add_function(state, api, "get_integer", &get_integer);
    add_function(state, api, "get_int", &get_integer);
    add_function(state, api, "get_unsigned", &get_unsigned);
    add_function(state, api, "get_uint", &get_unsigned);
    add_function(state, api, "get_float", &get_float);
    add_function(state, api, "get_string", &get_string);
    add_function(state, api, "get_vector", &get_vector);
    add_function(state, api, "get_angle", &get_angle);
    add_function(state, api, "get_handle", &get_handle);
    add_function(state, api, "get_pointer", &get_pointer);

    add_function(state, api, "set_boolean", &set_boolean);
    add_function(state, api, "set_bool", &set_boolean);
    add_function(state, api, "set_integer", &set_integer);
    add_function(state, api, "set_int", &set_integer);
    add_function(state, api, "set_unsigned", &set_unsigned);
    add_function(state, api, "set_uint", &set_unsigned);
    add_function(state, api, "set_float", &set_float);
    add_function(state, api, "set_string", &set_string);
    add_function(state, api, "set_vector", &set_vector);
    add_function(state, api, "set_angle", &set_angle);
    add_function(state, api, "set_handle", &set_handle);
    add_function(state, api, "set_pointer", &set_pointer);

    add_constant(state, "BOOLEAN", PropertyKind::Boolean);
    add_constant(state, "SIGNED_INTEGER", PropertyKind::SignedInteger);
    add_constant(state, "UNSIGNED_INTEGER", PropertyKind::UnsignedInteger);
    add_constant(state, "FLOAT", PropertyKind::Float);
    add_constant(state, "STRING", PropertyKind::String);
    add_constant(state, "VECTOR", PropertyKind::Vector);
    add_constant(state, "ANGLE", PropertyKind::Angle);
    add_constant(state, "ENTITY_HANDLE", PropertyKind::EntityHandle);
    add_constant(state, "POINTER", PropertyKind::Pointer);
    add_constant(state, "RAW", PropertyKind::Raw);
    add_kind_map(state);
    return 1;
}
