// Reuse the established constants and helpers, but replace the exported module
// entry point so ABI v3 exposes every Source 2 Ray_t shape and result field.
#define LuaCS_OpenModule LuaCS_OpenModule_V2
#include "traces.cpp"
#undef LuaCS_OpenModule

namespace {

TraceShape complete_shape(lua_State* state, int table,
                          TraceShape fallback = TraceShape::Line) {
    lua_getfield(state, table, "shape");
    if (lua_isnil(state, -1)) {
        lua_pop(state, 1);
        return fallback;
    }
    const lua_Integer value = luaL_checkinteger(state, -1);
    lua_pop(state, 1);
    if (value < static_cast<lua_Integer>(TraceShape::Line) ||
        value > static_cast<lua_Integer>(TraceShape::Mesh)) {
        luaL_error(state, "trace shape is out of range");
    }
    return static_cast<TraceShape>(value);
}

void read_mesh_vertices(lua_State* state, int index, TraceRequest& request) {
    luaL_checktype(state, index, LUA_TTABLE);
    const lua_Integer length = luaL_len(state, index);
    if (length < 3 ||
        length > static_cast<lua_Integer>(luacs::kTraceMeshVertexCapacity)) {
        luaL_error(state, "mesh trace requires 3 to %d vertices",
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

bool read_optional_mesh_vertices(lua_State* state, int table,
                                 TraceRequest& request) {
    lua_getfield(state, table, "vertices");
    if (lua_isnil(state, -1)) {
        lua_pop(state, 1);
        return false;
    }
    read_mesh_vertices(state, -1, request);
    lua_pop(state, 1);
    return true;
}

void read_complete_options(lua_State* state, int table,
                           TraceRequest& request) {
    if (!lua_istable(state, table)) return;

    request.contents_mask =
        read_u64_field(state, table, "mask", request.contents_mask);
    request.contents =
        read_u64_field(state, table, "contents", request.contents);
    request.interacts_with = read_u64_field(
        state, table, "interacts_with", request.interacts_with);
    request.interacts_exclude = read_u64_field(
        state, table, "interacts_exclude", request.interacts_exclude);
    request.interacts_as =
        read_u64_field(state, table, "interacts_as", request.interacts_as);
    request.collision_group = read_u64_field(
        state, table, "collision_group", request.collision_group);
    request.ignore_entities_mask = read_u64_field(
        state, table, "object_set_mask", request.ignore_entities_mask);
    request.radius =
        read_float_field(state, table, "radius", request.radius);
    request.hit_triggers = read_bool_field(
        state, table, "hit_triggers", request.hit_triggers);
    request.hit_solid =
        read_bool_field(state, table, "hit_solid", request.hit_solid);
    request.hit_solid_requires_generate_contacts = read_bool_field(
        state, table, "hit_solid_requires_generate_contacts",
        request.hit_solid_requires_generate_contacts);
    request.ignore_disabled_pairs = read_bool_field(
        state, table, "ignore_disabled_pairs",
        request.ignore_disabled_pairs);
    request.ignore_if_both_hitboxes = read_bool_field(
        state, table, "ignore_if_both_hitboxes",
        request.ignore_if_both_hitboxes);
    request.force_hit_everything = read_bool_field(
        state, table, "force_hit_everything",
        request.force_hit_everything);
    request.iterate_entities = read_bool_field(
        state, table, "iterate_entities", request.iterate_entities);
    request.hit_entities = read_bool_field(
        state, table, "hit_entities", request.hit_entities);
    request.included_detail_layers = static_cast<std::uint16_t>(
        read_u64_field(state, table, "included_detail_layers",
                       request.included_detail_layers));
    request.target_detail_layer = static_cast<std::uint8_t>(
        read_u64_field(state, table, "target_detail_layer",
                       request.target_detail_layer));

    read_optional_vector(state, table, "mins", request.mins);
    read_optional_vector(state, table, "maxs", request.maxs);
    if (!read_optional_vector(state, table, "center", request.center_a)) {
        read_optional_vector(state, table, "center_a", request.center_a);
    }
    read_optional_vector(state, table, "center_b", request.center_b);
    read_optional_mesh_vertices(state, table, request);

    lua_getfield(state, table, "ignore");
    read_ignore_value(state, -1, request);
    lua_pop(state, 1);
    lua_getfield(state, table, "ignore_entities");
    read_ignore_value(state, -1, request);
    lua_pop(state, 1);
}

void push_complete_result(lua_State* state, const TraceResult& result) {
    push_result(state, result);
#define SET_BOOL(name)                                                         \
    lua_pushboolean(state, result.name);                                       \
    lua_setfield(state, -2, #name)
#define SET_INT(name)                                                          \
    lua_pushinteger(state,                                                     \
                    static_cast<lua_Integer>(result.name));                    \
    lua_setfield(state, -2, #name)
    SET_BOOL(fraction_left_solid_available);
    SET_INT(contents64);
    SET_INT(physics_body);
    SET_INT(physics_shape);
    SET_INT(shape_interacts_as);
    SET_INT(shape_interacts_with);
    SET_INT(shape_interacts_exclude);
    SET_INT(shape_entity_id);
    SET_INT(shape_owner_id);
    SET_INT(shape_hierarchy_id);
    SET_INT(shape_detail_layer_mask);
    SET_INT(shape_detail_layer_mask_type);
    SET_INT(shape_target_detail_layer);
    SET_INT(shape_collision_group);
    SET_INT(shape_collision_function_mask);
#undef SET_INT
#undef SET_BOOL
}

int run_complete(lua_State* state, const TraceRequest& request) {
    const auto* api = advanced();
    TraceResult result;
    char error[512]{};
    if (!api || !api->trace ||
        !api->trace(api->context, &request, &result, error,
                    sizeof(error))) {
        return fail(state,
                    error[0] ? error : "trace service is unavailable");
    }
    push_complete_result(state, result);
    return 1;
}

int complete_line(lua_State* state) {
    TraceRequest request;
    request.start = read_vector(state, 1);
    request.end = read_vector(state, 2);
    read_complete_options(state, 3, request);
    request.shape = TraceShape::Line;
    request.use_hull = false;
    return run_complete(state, request);
}

int complete_sphere(lua_State* state) {
    TraceRequest request;
    request.start = read_vector(state, 1);
    request.end = read_vector(state, 2);
    request.radius = static_cast<float>(luaL_checknumber(state, 3));
    read_complete_options(state, 4, request);
    request.shape = TraceShape::Sphere;
    request.use_hull = false;
    return run_complete(state, request);
}

int complete_hull(lua_State* state) {
    TraceRequest request;
    request.start = read_vector(state, 1);
    request.end = read_vector(state, 2);
    request.mins = read_vector(state, 3);
    request.maxs = read_vector(state, 4);
    read_complete_options(state, 5, request);
    request.shape = TraceShape::Hull;
    request.use_hull = true;
    return run_complete(state, request);
}

int complete_capsule(lua_State* state) {
    TraceRequest request;
    request.start = read_vector(state, 1);
    request.end = read_vector(state, 2);
    request.center_a = read_vector(state, 3);
    request.center_b = read_vector(state, 4);
    request.radius = static_cast<float>(luaL_checknumber(state, 5));
    read_complete_options(state, 6, request);
    request.shape = TraceShape::Capsule;
    request.use_hull = false;
    return run_complete(state, request);
}

int complete_mesh(lua_State* state) {
    TraceRequest request;
    request.start = read_vector(state, 1);
    request.end = read_vector(state, 2);
    request.mins = read_vector(state, 3);
    request.maxs = read_vector(state, 4);
    read_mesh_vertices(state, 5, request);
    read_complete_options(state, 6, request);
    request.shape = TraceShape::Mesh;
    request.use_hull = false;
    return run_complete(state, request);
}

int complete_cast(lua_State* state) {
    luaL_checktype(state, 1, LUA_TTABLE);
    TraceRequest request;
    if (!read_optional_vector(state, 1, "start", request.start) ||
        !read_optional_vector(state, 1, "end", request.end)) {
        return luaL_error(state,
                          "trace request requires start and end vectors");
    }
    const TraceShape shape = complete_shape(state, 1);
    read_complete_options(state, 1, request);
    request.shape = shape;
    request.use_hull = shape == TraceShape::Hull;
    if (shape == TraceShape::Mesh && request.mesh_vertex_count == 0) {
        return luaL_error(state,
                          "mesh trace request requires a vertices array");
    }
    return run_complete(state, request);
}

void replace_function(lua_State* state, const Services* api,
                      const char* name, lua_CFunction function) {
    add_function(state, api, name, function);
}

} // namespace

LUACS_MODULE_EXPORT int LuaCS_OpenModule(lua_State* state,
                                         const Services* api) {
    const int results = LuaCS_OpenModule_V2(state, api);
    if (results != 1 || !lua_istable(state, -1)) return results;

    replace_function(state, api, "cast", &complete_cast);
    replace_function(state, api, "ray", &complete_line);
    replace_function(state, api, "line", &complete_line);
    replace_function(state, api, "sphere", &complete_sphere);
    replace_function(state, api, "hull", &complete_hull);
    replace_function(state, api, "capsule", &complete_capsule);
    replace_function(state, api, "mesh", &complete_mesh);
    add_mask(state, "SHAPE_MESH",
             static_cast<std::uint64_t>(TraceShape::Mesh));
    add_mask(state, "MAX_IGNORE_ENTITIES",
             luacs::kTraceIgnoreCapacity);
    add_mask(state, "MAX_MESH_VERTICES",
             luacs::kTraceMeshVertexCapacity);
    return 1;
}
