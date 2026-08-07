#include "luacs/advanced_world_module.h"
#include "luacs/module_api.h"

extern "C" {
#include "lauxlib.h"
}

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>

namespace {
using luacs::AdvancedWorldServices;
using luacs::Services;
using luacs::TraceRequest;
using luacs::TraceResult;
using luacs::TraceShape;
using luacs::Vector3;

inline constexpr const char* kTraceResultMeta = "LuaCS.TraceResult";

const AdvancedWorldServices* advanced() {
    return luacs::resolve_advanced_world_services();
}

int fail(lua_State* state, const char* error) {
    lua_pushnil(state);
    lua_pushstring(state, error && *error ? error : "trace operation failed");
    return 2;
}

bool finite_vector(const Vector3& value) {
    return std::isfinite(value.x) && std::isfinite(value.y) &&
           std::isfinite(value.z);
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
        luaL_argerror(state, index, "trace vector components must be finite");
    }
    return value;
}

bool read_optional_vector(lua_State* state, int table, const char* key,
                          Vector3& output) {
    table = lua_absindex(state, table);
    lua_getfield(state, table, key);
    if (lua_isnil(state, -1)) {
        lua_pop(state, 1);
        return false;
    }
    output = read_vector(state, -1);
    lua_pop(state, 1);
    return true;
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

void push_u64_exact(lua_State* state, std::uint64_t value) {
    if (value <= static_cast<std::uint64_t>(
                     std::numeric_limits<lua_Integer>::max())) {
        lua_pushinteger(state, static_cast<lua_Integer>(value));
        return;
    }
    const std::string exact = std::to_string(value);
    lua_pushlstring(state, exact.data(), exact.size());
}

std::uint64_t read_u64_exact(lua_State* state, int index, const char* label) {
    if (lua_isinteger(state, index)) {
        const lua_Integer value = lua_tointeger(state, index);
        if (value < 0) {
            luaL_argerror(state, index, "64-bit trace values cannot be negative");
        }
        return static_cast<std::uint64_t>(value);
    }

    std::size_t size{};
    const char* raw = luaL_checklstring(state, index, &size);
    if (!raw || size == 0 || raw[0] == '-') {
        luaL_argerror(state, index,
                      "expected a non-negative integer or decimal/0x string");
    }
    const std::string text(raw, size);
    try {
        std::size_t parsed{};
        const bool hexadecimal =
            text.size() > 2 && text[0] == '0' &&
            (text[1] == 'x' || text[1] == 'X');
        const std::uint64_t value =
            std::stoull(text, &parsed, hexadecimal ? 16 : 10);
        if (parsed != text.size()) {
            luaL_argerror(state, index,
                          "64-bit trace value contains trailing characters");
        }
        return value;
    } catch (...) {
        std::string message = "invalid ";
        message += label;
        message += "; expected a non-negative integer or decimal/0x string";
        luaL_argerror(state, index, message.c_str());
    }
    return 0;
}

std::uint64_t read_u64_field(lua_State* state, int table, const char* key,
                             std::uint64_t fallback) {
    table = lua_absindex(state, table);
    lua_getfield(state, table, key);
    if (lua_isnil(state, -1)) {
        lua_pop(state, 1);
        return fallback;
    }
    const std::uint64_t value = read_u64_exact(state, -1, key);
    lua_pop(state, 1);
    return value;
}

std::uint16_t read_u16_field(lua_State* state, int table, const char* key,
                             std::uint16_t fallback) {
    table = lua_absindex(state, table);
    lua_getfield(state, table, key);
    if (lua_isnil(state, -1)) {
        lua_pop(state, 1);
        return fallback;
    }
    const lua_Integer value = luaL_checkinteger(state, -1);
    lua_pop(state, 1);
    if (value < 0 ||
        value > static_cast<lua_Integer>(
                    std::numeric_limits<std::uint16_t>::max())) {
        luaL_error(state, "%s must fit in an unsigned 16-bit integer", key);
    }
    return static_cast<std::uint16_t>(value);
}

std::uint8_t read_u8_field(lua_State* state, int table, const char* key,
                           std::uint8_t fallback) {
    table = lua_absindex(state, table);
    lua_getfield(state, table, key);
    if (lua_isnil(state, -1)) {
        lua_pop(state, 1);
        return fallback;
    }
    const lua_Integer value = luaL_checkinteger(state, -1);
    lua_pop(state, 1);
    if (value < 0 ||
        value > static_cast<lua_Integer>(
                    std::numeric_limits<std::uint8_t>::max())) {
        luaL_error(state, "%s must fit in an unsigned 8-bit integer", key);
    }
    return static_cast<std::uint8_t>(value);
}

float read_float_field(lua_State* state, int table, const char* key,
                       float fallback) {
    table = lua_absindex(state, table);
    lua_getfield(state, table, key);
    if (lua_isnil(state, -1)) {
        lua_pop(state, 1);
        return fallback;
    }
    const float value = static_cast<float>(luaL_checknumber(state, -1));
    lua_pop(state, 1);
    if (!std::isfinite(value)) {
        luaL_error(state, "%s must be finite", key);
    }
    return value;
}

bool read_bool_field(lua_State* state, int table, const char* key,
                     bool fallback) {
    table = lua_absindex(state, table);
    lua_getfield(state, table, key);
    if (lua_isnil(state, -1)) {
        lua_pop(state, 1);
        return fallback;
    }
    luaL_checktype(state, -1, LUA_TBOOLEAN);
    const bool value = lua_toboolean(state, -1) != 0;
    lua_pop(state, 1);
    return value;
}

const char* shape_name(TraceShape shape) {
    switch (shape) {
        case TraceShape::Line: return "line";
        case TraceShape::Sphere: return "sphere";
        case TraceShape::Hull: return "hull";
        case TraceShape::Capsule: return "capsule";
        case TraceShape::Mesh: return "mesh";
        default: return "unknown";
    }
}

TraceShape read_shape_value(lua_State* state, int index) {
    if (lua_isinteger(state, index)) {
        const lua_Integer value = lua_tointeger(state, index);
        if (value < static_cast<lua_Integer>(TraceShape::Line) ||
            value > static_cast<lua_Integer>(TraceShape::Mesh)) {
            luaL_argerror(state, index, "trace shape is out of range");
        }
        return static_cast<TraceShape>(value);
    }
    const std::string_view value = luaL_checkstring(state, index);
    if (value == "line" || value == "ray") return TraceShape::Line;
    if (value == "sphere") return TraceShape::Sphere;
    if (value == "hull" || value == "box") return TraceShape::Hull;
    if (value == "capsule") return TraceShape::Capsule;
    if (value == "mesh") return TraceShape::Mesh;
    luaL_argerror(state, index,
                  "trace shape must be line, sphere, hull, capsule, or mesh");
    return TraceShape::Line;
}

TraceShape read_shape_field(lua_State* state, int table,
                            TraceShape fallback) {
    table = lua_absindex(state, table);
    lua_getfield(state, table, "shape");
    if (lua_isnil(state, -1)) {
        lua_pop(state, 1);
        return fallback;
    }
    const TraceShape value = read_shape_value(state, -1);
    lua_pop(state, 1);
    return value;
}

void append_ignore(lua_State* state, TraceRequest& request, int value) {
    if (value < 0) return;
    for (std::size_t index = 0; index < request.ignore_count; ++index) {
        if (request.ignore_entities[index] == value) return;
    }
    if (request.ignore_count >= luacs::kTraceIgnoreCapacity) {
        luaL_error(state, "trace ignore list exceeds %d entities",
                   static_cast<int>(luacs::kTraceIgnoreCapacity));
    }
    request.ignore_entities[request.ignore_count++] = value;
}

void read_ignore_value(lua_State* state, int index, TraceRequest& request) {
    if (lua_isnoneornil(state, index)) return;
    if (lua_isinteger(state, index)) {
        append_ignore(state, request, entity_index_from(state, index, true));
        return;
    }
    luaL_checktype(state, index, LUA_TTABLE);

    lua_Integer direct{};
    if (integer_field(state, index, "entity_index", direct) ||
        (integer_field(state, index, "pawn_index", direct) && direct >= 0) ||
        integer_field(state, index, "controller_index", direct)) {
        append_ignore(state, request,
                      checked_entity_index(state, index, direct, true));
        return;
    }

    const lua_Integer length = luaL_len(state, index);
    for (lua_Integer item = 1; item <= length; ++item) {
        lua_geti(state, index, item);
        append_ignore(state, request, entity_index_from(state, -1, true));
        lua_pop(state, 1);
    }
}

void read_mesh_table(lua_State* state, int index, TraceRequest& request) {
    luaL_checktype(state, index, LUA_TTABLE);
    const lua_Integer length = luaL_len(state, index);
    if (length < 0 ||
        static_cast<std::uint64_t>(length) >
            static_cast<std::uint64_t>(luacs::kTraceMeshVertexCapacity)) {
        luaL_error(state, "trace mesh exceeds %d vertices",
                   static_cast<int>(luacs::kTraceMeshVertexCapacity));
    }
    request.mesh_vertex_count = static_cast<std::size_t>(length);
    for (lua_Integer item = 1; item <= length; ++item) {
        lua_geti(state, index, item);
        request.mesh_vertices[static_cast<std::size_t>(item - 1)] =
            read_vector(state, -1);
        lua_pop(state, 1);
    }
}

bool read_optional_mesh(lua_State* state, int table, const char* key,
                        TraceRequest& request) {
    table = lua_absindex(state, table);
    lua_getfield(state, table, key);
    if (lua_isnil(state, -1)) {
        lua_pop(state, 1);
        return false;
    }
    read_mesh_table(state, -1, request);
    lua_pop(state, 1);
    return true;
}

void read_options(lua_State* state, int table, TraceRequest& request) {
    if (lua_isnoneornil(state, table)) return;
    luaL_checktype(state, table, LUA_TTABLE);
    table = lua_absindex(state, table);

    request.shape = read_shape_field(state, table, request.shape);
    request.contents_mask =
        read_u64_field(state, table, "mask", request.contents_mask);
    request.contents =
        read_u64_field(state, table, "contents", request.contents);
    request.interacts_with =
        read_u64_field(state, table, "interacts_with", request.interacts_with);
    request.interacts_exclude = read_u64_field(
        state, table, "interacts_exclude", request.interacts_exclude);
    request.interacts_as =
        read_u64_field(state, table, "interacts_as", request.interacts_as);
    request.collision_group = read_u64_field(
        state, table, "collision_group", request.collision_group);
    request.ignore_entities_mask = read_u64_field(
        state, table, "object_set_mask", request.ignore_entities_mask);
    request.radius = read_float_field(state, table, "radius", request.radius);
    request.included_detail_layers = read_u16_field(
        state, table, "included_detail_layers",
        request.included_detail_layers);
    request.target_detail_layer = read_u8_field(
        state, table, "target_detail_layer", request.target_detail_layer);
    request.hit_triggers =
        read_bool_field(state, table, "hit_triggers", request.hit_triggers);
    request.hit_solid =
        read_bool_field(state, table, "hit_solid", request.hit_solid);
    request.hit_solid_requires_generate_contacts = read_bool_field(
        state, table, "hit_solid_requires_generate_contacts",
        request.hit_solid_requires_generate_contacts);
    request.ignore_disabled_pairs = read_bool_field(
        state, table, "ignore_disabled_pairs", request.ignore_disabled_pairs);
    request.ignore_if_both_hitboxes = read_bool_field(
        state, table, "ignore_if_both_hitboxes",
        request.ignore_if_both_hitboxes);
    request.force_hit_everything = read_bool_field(
        state, table, "force_hit_everything", request.force_hit_everything);
    request.iterate_entities = read_bool_field(
        state, table, "iterate_entities", request.iterate_entities);
    request.hit_entities =
        read_bool_field(state, table, "hit_entities", request.hit_entities);
    read_optional_vector(state, table, "mins", request.mins);
    read_optional_vector(state, table, "maxs", request.maxs);
    read_optional_vector(state, table, "center", request.center_a);
    read_optional_vector(state, table, "center_a", request.center_a);
    read_optional_vector(state, table, "center_b", request.center_b);
    if (!read_optional_mesh(state, table, "mesh_vertices", request)) {
        read_optional_mesh(state, table, "vertices", request);
    }

    lua_getfield(state, table, "ignore");
    read_ignore_value(state, -1, request);
    lua_pop(state, 1);
    lua_getfield(state, table, "ignore_entities");
    read_ignore_value(state, -1, request);
    lua_pop(state, 1);
}

float vector_distance(const Vector3& left, const Vector3& right) {
    const float x = right.x - left.x;
    const float y = right.y - left.y;
    const float z = right.z - left.z;
    return std::sqrt(x * x + y * y + z * z);
}

void push_result(lua_State* state, const TraceResult& result) {
    lua_createtable(state, 0, 58);
#define SET_BOOL(name)                                                         \
    lua_pushboolean(state, result.name);                                       \
    lua_setfield(state, -2, #name)
#define SET_INT(name)                                                          \
    lua_pushinteger(state, static_cast<lua_Integer>(result.name));             \
    lua_setfield(state, -2, #name)
#define SET_NUM(name)                                                          \
    lua_pushnumber(state, result.name);                                        \
    lua_setfield(state, -2, #name)
    SET_BOOL(valid);
    SET_BOOL(hit);
    SET_BOOL(start_solid);
    SET_BOOL(all_solid);
    SET_BOOL(exact_hit_point);
    SET_BOOL(fraction_left_solid_available);
    SET_NUM(fraction);
    SET_NUM(fraction_left_solid);
    SET_NUM(plane_distance);
    SET_NUM(distance);
    SET_NUM(hit_offset);
    SET_INT(entity_index);
    SET_INT(entity_handle);
    SET_INT(hitbox);
    SET_INT(hitgroup);
    SET_INT(surface_flags);
    SET_INT(contents);
    SET_INT(triangle);
    SET_INT(bone);
    SET_INT(shape_entity_id);
    SET_INT(shape_owner_id);
    SET_INT(shape_hierarchy_id);
    SET_INT(shape_detail_layer_mask);
    SET_INT(shape_detail_layer_mask_type);
    SET_INT(shape_target_detail_layer);
    SET_INT(shape_collision_group);
    SET_INT(shape_collision_function_mask);
#undef SET_NUM
#undef SET_INT
#undef SET_BOOL

    push_u64_exact(state, result.contents64);
    lua_setfield(state, -2, "contents64");
    push_u64_exact(state, static_cast<std::uint64_t>(result.physics_body));
    lua_setfield(state, -2, "physics_body");
    push_u64_exact(state, static_cast<std::uint64_t>(result.physics_shape));
    lua_setfield(state, -2, "physics_shape");
    push_u64_exact(state, result.shape_interacts_as);
    lua_setfield(state, -2, "shape_interacts_as");
    push_u64_exact(state, result.shape_interacts_with);
    lua_setfield(state, -2, "shape_interacts_with");
    push_u64_exact(state, result.shape_interacts_exclude);
    lua_setfield(state, -2, "shape_interacts_exclude");

    lua_pushinteger(state, static_cast<lua_Integer>(result.shape));
    lua_setfield(state, -2, "shape");
    lua_pushstring(state, shape_name(result.shape));
    lua_setfield(state, -2, "shape_name");

    push_vector(state, result.start);
    lua_setfield(state, -2, "start");
    push_vector(state, result.end);
    lua_setfield(state, -2, "end_position");
    push_vector(state, result.end);
    lua_setfield(state, -2, "end");
    push_vector(state, result.hit_position);
    lua_setfield(state, -2, "position");
    push_vector(state, result.hit_position);
    lua_setfield(state, -2, "hit_position");
    push_vector(state, result.plane_normal);
    lua_setfield(state, -2, "normal");
    push_vector(state, result.plane_normal);
    lua_setfield(state, -2, "plane_normal");
    lua_pushstring(state, result.surface_name);
    lua_setfield(state, -2, "surface_name");

    const float total_distance = vector_distance(result.start, result.end);
    lua_pushnumber(state, total_distance);
    lua_setfield(state, -2, "total_distance");
    lua_pushnumber(state, std::max(0.0f, total_distance - result.distance));
    lua_setfield(state, -2, "remaining_distance");
    lua_pushboolean(state, result.hit && result.entity_index >= 0);
    lua_setfield(state, -2, "hit_entity");
    lua_pushboolean(state, result.hit && result.entity_index < 0);
    lua_setfield(state, -2, "hit_world");

    luaL_getmetatable(state, kTraceResultMeta);
    lua_setmetatable(state, -2);
}

int run(lua_State* state, const TraceRequest& request) {
    const auto* api = advanced();
    TraceResult result;
    char error[512]{};
    if (!api || !api->trace ||
        !api->trace(api->context, &request, &result, error, sizeof(error))) {
        return fail(state, error[0] ? error : "trace service is unavailable");
    }
    if (!result.valid || !std::isfinite(result.fraction) ||
        result.fraction < 0.0f || result.fraction > 1.0f ||
        !std::isfinite(result.distance) || result.distance < 0.0f ||
        !finite_vector(result.start) || !finite_vector(result.end) ||
        !finite_vector(result.hit_position) ||
        !finite_vector(result.plane_normal)) {
        return fail(state,
                    "Source 2 returned an invalid or non-finite trace result");
    }
    push_result(state, result);
    return 1;
}

int line(lua_State* state) {
    TraceRequest request;
    request.start = read_vector(state, 1);
    request.end = read_vector(state, 2);
    read_options(state, 3, request);
    request.shape = TraceShape::Line;
    request.use_hull = false;
    return run(state, request);
}

int hull(lua_State* state) {
    TraceRequest request;
    request.start = read_vector(state, 1);
    request.end = read_vector(state, 2);
    request.mins = read_vector(state, 3);
    request.maxs = read_vector(state, 4);
    read_options(state, 5, request);
    request.shape = TraceShape::Hull;
    request.use_hull = true;
    return run(state, request);
}

int sphere(lua_State* state) {
    TraceRequest request;
    request.start = read_vector(state, 1);
    request.end = read_vector(state, 2);
    request.center_a = read_vector(state, 3);
    request.radius = static_cast<float>(luaL_checknumber(state, 4));
    if (!std::isfinite(request.radius)) {
        return luaL_argerror(state, 4, "sphere radius must be finite");
    }
    read_options(state, 5, request);
    request.shape = TraceShape::Sphere;
    request.use_hull = false;
    return run(state, request);
}

int capsule(lua_State* state) {
    TraceRequest request;
    request.start = read_vector(state, 1);
    request.end = read_vector(state, 2);
    request.center_a = read_vector(state, 3);
    request.center_b = read_vector(state, 4);
    request.radius = static_cast<float>(luaL_checknumber(state, 5));
    if (!std::isfinite(request.radius)) {
        return luaL_argerror(state, 5, "capsule radius must be finite");
    }
    read_options(state, 6, request);
    request.shape = TraceShape::Capsule;
    request.use_hull = false;
    return run(state, request);
}

int mesh(lua_State* state) {
    TraceRequest request;
    request.start = read_vector(state, 1);
    request.end = read_vector(state, 2);
    request.mins = read_vector(state, 3);
    request.maxs = read_vector(state, 4);
    read_mesh_table(state, 5, request);
    read_options(state, 6, request);
    request.shape = TraceShape::Mesh;
    request.use_hull = false;
    return run(state, request);
}

int cast(lua_State* state) {
    luaL_checktype(state, 1, LUA_TTABLE);
    TraceRequest request;
    if (!read_optional_vector(state, 1, "start", request.start) ||
        !read_optional_vector(state, 1, "end", request.end)) {
        return luaL_error(state, "trace request requires start and end vectors");
    }
    read_options(state, 1, request);
    request.use_hull = request.shape == TraceShape::Hull;
    return run(state, request);
}

int direction(lua_State* state) {
    TraceRequest request;
    request.start = read_vector(state, 1);
    const Vector3 direction_value = read_vector(state, 2);
    const float distance = static_cast<float>(luaL_checknumber(state, 3));
    if (!std::isfinite(distance) || distance < 0.0f) {
        return luaL_argerror(state, 3,
                             "trace direction distance must be finite and non-negative");
    }
    const float length = std::sqrt(
        direction_value.x * direction_value.x +
        direction_value.y * direction_value.y +
        direction_value.z * direction_value.z);
    if (distance > 0.0f && (!std::isfinite(length) || length <= 0.0f)) {
        return luaL_argerror(state, 2,
                             "trace direction must be non-zero for a positive distance");
    }
    if (distance == 0.0f) {
        request.end = request.start;
    } else {
        const float scale = distance / length;
        request.end = {request.start.x + direction_value.x * scale,
                       request.start.y + direction_value.y * scale,
                       request.start.z + direction_value.z * scale};
    }
    read_options(state, 4, request);
    request.shape = TraceShape::Line;
    request.use_hull = false;
    return run(state, request);
}

int ray(lua_State* state) { return line(state); }

bool result_bool(lua_State* state, const char* key) {
    lua_getfield(state, 1, key);
    const bool value = lua_toboolean(state, -1) != 0;
    lua_pop(state, 1);
    return value;
}

int result_entity(lua_State* state) {
    lua_getfield(state, 1, "entity_index");
    const int value = static_cast<int>(luaL_checkinteger(state, -1));
    lua_pop(state, 1);
    return value;
}

int result_did_hit(lua_State* state) {
    luaL_checktype(state, 1, LUA_TTABLE);
    lua_pushboolean(state, result_bool(state, "hit"));
    return 1;
}

int result_hit_world(lua_State* state) {
    luaL_checktype(state, 1, LUA_TTABLE);
    lua_pushboolean(state,
                    result_bool(state, "hit") && result_entity(state) < 0);
    return 1;
}

int result_hit_entity(lua_State* state) {
    luaL_checktype(state, 1, LUA_TTABLE);
    const int hit_entity = result_entity(state);
    if (lua_isnoneornil(state, 2)) {
        lua_pushboolean(state,
                        result_bool(state, "hit") && hit_entity >= 0);
        return 1;
    }
    const int expected = entity_index_from(state, 2);
    lua_pushboolean(state,
                    result_bool(state, "hit") && hit_entity == expected);
    return 1;
}

int result_tostring(lua_State* state) {
    luaL_checktype(state, 1, LUA_TTABLE);
    lua_getfield(state, 1, "shape_name");
    const char* shape = luaL_checkstring(state, -1);
    lua_pop(state, 1);
    lua_getfield(state, 1, "fraction");
    const lua_Number fraction = luaL_checknumber(state, -1);
    lua_pop(state, 1);
    lua_pushfstring(state, "TraceResult(%s, hit=%s, fraction=%.4f)", shape,
                    result_bool(state, "hit") ? "true" : "false",
                    static_cast<double>(fraction));
    return 1;
}

void add_function(lua_State* state, const Services* api, const char* name,
                  lua_CFunction function) {
    lua_pushlightuserdata(state, const_cast<Services*>(api));
    lua_pushcclosure(state, function, 1);
    lua_setfield(state, -2, name);
}

void add_mask(lua_State* state, const char* name, std::uint64_t value) {
    push_u64_exact(state, value);
    lua_setfield(state, -2, name);
}

constexpr std::uint64_t TRACE_SOLID = 0x1ull;
constexpr std::uint64_t TRACE_HITBOXES = 0x2ull;
constexpr std::uint64_t TRACE_TRIGGER = 0x4ull;
constexpr std::uint64_t TRACE_SKY = 0x8ull;
constexpr std::uint64_t TRACE_PLAYER_CLIP = 0x10ull;
constexpr std::uint64_t TRACE_NPC_CLIP = 0x20ull;
constexpr std::uint64_t TRACE_BLOCK_LOS = 0x40ull;
constexpr std::uint64_t TRACE_BLOCK_LIGHT = 0x80ull;
constexpr std::uint64_t TRACE_LADDER = 0x100ull;
constexpr std::uint64_t TRACE_PICKUP = 0x200ull;
constexpr std::uint64_t TRACE_BLOCK_SOUND = 0x400ull;
constexpr std::uint64_t TRACE_NODRAW = 0x800ull;
constexpr std::uint64_t TRACE_WINDOW = 0x1000ull;
constexpr std::uint64_t TRACE_PASS_BULLETS = 0x2000ull;
constexpr std::uint64_t TRACE_WORLD_GEOMETRY = 0x4000ull;
constexpr std::uint64_t TRACE_WATER = 0x8000ull;
constexpr std::uint64_t TRACE_SLIME = 0x10000ull;
constexpr std::uint64_t TRACE_TOUCH_ALL = 0x20000ull;
constexpr std::uint64_t TRACE_PLAYER = 0x40000ull;
constexpr std::uint64_t TRACE_NPC = 0x80000ull;
constexpr std::uint64_t TRACE_DEBRIS = 0x100000ull;
constexpr std::uint64_t TRACE_PHYSICS_PROP = 0x200000ull;
constexpr std::uint64_t TRACE_NAV_IGNORE = 0x400000ull;
constexpr std::uint64_t TRACE_NAV_LOCAL_IGNORE = 0x800000ull;
constexpr std::uint64_t TRACE_POST_PROCESSING_VOLUME = 0x1000000ull;
constexpr std::uint64_t TRACE_CARRIED_OBJECT = 0x4000000ull;
constexpr std::uint64_t TRACE_PUSH_AWAY = 0x8000000ull;
constexpr std::uint64_t TRACE_SERVER_ENTITY_ON_CLIENT = 0x10000000ull;
constexpr std::uint64_t TRACE_CARRIED_WEAPON = 0x20000000ull;
constexpr std::uint64_t TRACE_STATIC_LEVEL = 0x40000000ull;
constexpr std::uint64_t TRACE_CSGO_TEAM1 = 0x80000000ull;
constexpr std::uint64_t TRACE_CSGO_TEAM2 = 0x100000000ull;
constexpr std::uint64_t TRACE_CSGO_GRENADE_CLIP = 0x200000000ull;
constexpr std::uint64_t TRACE_CSGO_DRONE_CLIP = 0x400000000ull;
constexpr std::uint64_t TRACE_CSGO_MOVEABLE = 0x800000000ull;
constexpr std::uint64_t TRACE_CSGO_OPAQUE = 0x1000000000ull;
constexpr std::uint64_t TRACE_CSGO_MONSTER = 0x2000000000ull;
constexpr std::uint64_t TRACE_CSGO_THROWN_GRENADE = 0x8000000000ull;

constexpr std::uint64_t MASK_SHOT_PHYSICS =
    TRACE_SOLID | TRACE_PLAYER_CLIP | TRACE_WINDOW | TRACE_PASS_BULLETS |
    TRACE_PLAYER | TRACE_NPC | TRACE_PHYSICS_PROP;
constexpr std::uint64_t MASK_SHOT_HITBOX =
    TRACE_HITBOXES | TRACE_PLAYER | TRACE_NPC;
constexpr std::uint64_t MASK_SHOT_FULL = MASK_SHOT_PHYSICS | TRACE_HITBOXES;
constexpr std::uint64_t MASK_WORLD_ONLY =
    TRACE_SOLID | TRACE_WINDOW | TRACE_PASS_BULLETS;
constexpr std::uint64_t MASK_GRENADE =
    TRACE_SOLID | TRACE_WINDOW | TRACE_PHYSICS_PROP | TRACE_PASS_BULLETS;
constexpr std::uint64_t MASK_PLAYER_MOVE =
    TRACE_SOLID | TRACE_WINDOW | TRACE_PLAYER_CLIP | TRACE_PASS_BULLETS;

void add_trace_constants(lua_State* state) {
#define ADD(name) add_mask(state, #name, name)
    ADD(TRACE_SOLID);
    ADD(TRACE_HITBOXES);
    ADD(TRACE_TRIGGER);
    ADD(TRACE_SKY);
    ADD(TRACE_PLAYER_CLIP);
    ADD(TRACE_NPC_CLIP);
    ADD(TRACE_BLOCK_LOS);
    ADD(TRACE_BLOCK_LIGHT);
    ADD(TRACE_LADDER);
    ADD(TRACE_PICKUP);
    ADD(TRACE_BLOCK_SOUND);
    ADD(TRACE_NODRAW);
    ADD(TRACE_WINDOW);
    ADD(TRACE_PASS_BULLETS);
    ADD(TRACE_WORLD_GEOMETRY);
    ADD(TRACE_WATER);
    ADD(TRACE_SLIME);
    ADD(TRACE_TOUCH_ALL);
    ADD(TRACE_PLAYER);
    ADD(TRACE_NPC);
    ADD(TRACE_DEBRIS);
    ADD(TRACE_PHYSICS_PROP);
    ADD(TRACE_NAV_IGNORE);
    ADD(TRACE_NAV_LOCAL_IGNORE);
    ADD(TRACE_POST_PROCESSING_VOLUME);
    ADD(TRACE_CARRIED_OBJECT);
    ADD(TRACE_PUSH_AWAY);
    ADD(TRACE_SERVER_ENTITY_ON_CLIENT);
    ADD(TRACE_CARRIED_WEAPON);
    ADD(TRACE_STATIC_LEVEL);
    ADD(TRACE_CSGO_TEAM1);
    ADD(TRACE_CSGO_TEAM2);
    ADD(TRACE_CSGO_GRENADE_CLIP);
    ADD(TRACE_CSGO_DRONE_CLIP);
    ADD(TRACE_CSGO_MOVEABLE);
    ADD(TRACE_CSGO_OPAQUE);
    ADD(TRACE_CSGO_MONSTER);
    ADD(TRACE_CSGO_THROWN_GRENADE);
    ADD(MASK_SHOT_PHYSICS);
    ADD(MASK_SHOT_HITBOX);
    ADD(MASK_SHOT_FULL);
    ADD(MASK_WORLD_ONLY);
    ADD(MASK_GRENADE);
    ADD(MASK_PLAYER_MOVE);
#undef ADD
    add_mask(state, "MASK_ALL", 0xFFFFFFFFFFFFFFFFull);
    add_mask(state, "SHAPE_LINE", static_cast<std::uint64_t>(TraceShape::Line));
    add_mask(state, "SHAPE_SPHERE",
             static_cast<std::uint64_t>(TraceShape::Sphere));
    add_mask(state, "SHAPE_HULL", static_cast<std::uint64_t>(TraceShape::Hull));
    add_mask(state, "SHAPE_CAPSULE",
             static_cast<std::uint64_t>(TraceShape::Capsule));
    add_mask(state, "SHAPE_MESH", static_cast<std::uint64_t>(TraceShape::Mesh));
}
} // namespace

LUACS_MODULE_EXPORT int LuaCS_OpenModule(lua_State* state,
                                         const Services* api) {
    if (!api || api->abi_version != luacs::kModuleAbiVersion) {
        return luaL_error(state, "LuaCS traces ABI mismatch");
    }
    if (!advanced()) {
        return luaL_error(state, "LuaCS advanced world services are unavailable");
    }

    if (luaL_newmetatable(state, kTraceResultMeta)) {
        lua_newtable(state);
        add_function(state, api, "did_hit", &result_did_hit);
        add_function(state, api, "hit_world", &result_hit_world);
        add_function(state, api, "hit_entity", &result_hit_entity);
        lua_setfield(state, -2, "__index");
        add_function(state, api, "__tostring", &result_tostring);
    }
    lua_pop(state, 1);

    lua_createtable(state, 0, 80);
    add_function(state, api, "cast", &cast);
    add_function(state, api, "ray", &ray);
    add_function(state, api, "line", &line);
    add_function(state, api, "segment", &line);
    add_function(state, api, "direction", &direction);
    add_function(state, api, "from_direction", &direction);
    add_function(state, api, "sphere", &sphere);
    add_function(state, api, "hull", &hull);
    add_function(state, api, "box", &hull);
    add_function(state, api, "capsule", &capsule);
    add_function(state, api, "mesh", &mesh);
    add_trace_constants(state);
    return 1;
}
