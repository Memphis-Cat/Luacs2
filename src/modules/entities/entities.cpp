#include "luacs/lua_checked.h"
#include "luacs/module_api.h"
#include "luacs/world_module.h"

extern "C" {
#include "lauxlib.h"
}

#include <cmath>
#include <cstddef>
#include <limits>
#include <string>
#include <string_view>

namespace {

using luacs::EntityInfo;
using luacs::Services;
using luacs::Vector3;
using luacs::WorldServices;

inline constexpr const char* kEntityMeta = "LuaCS.Entity";

const Services* services(lua_State* state) {
    return static_cast<const Services*>(
        lua_touserdata(state, lua_upvalueindex(1)));
}

const WorldServices* world(lua_State* state) {
    return luacs::resolve_world_services(services(state));
}

int fail(lua_State* state, const char* error) {
    lua_pushnil(state);
    lua_pushstring(state, error && *error ? error : "entity operation failed");
    return 2;
}

int entity_index_from(lua_State* state, int index, bool allow_nil = false) {
    if (allow_nil && lua_isnoneornil(state, index)) return -1;
    if (lua_isinteger(state, index)) {
        return luacs::lua_checked::checked_entity_index(state, index, allow_nil);
    }
    luaL_checktype(state, index, LUA_TTABLE);
    const int table = lua_absindex(state, index);
    lua_getfield(state, table, "entity_index");
    if (!lua_isinteger(state, -1)) {
        lua_pop(state, 1);
        return luaL_argerror(
            state, index,
            "expected an entity table containing an integer entity_index");
    }
    const lua_Integer raw = lua_tointeger(state, -1);
    lua_pop(state, 1);
    const int minimum = allow_nil ? -1 : 0;
    if (raw < minimum || raw > std::numeric_limits<int>::max()) {
        return luaL_argerror(
            state, index,
            allow_nil
                ? "entity_index must be -1 or a non-negative 32-bit integer"
                : "entity_index must be a non-negative 32-bit integer");
    }
    return static_cast<int>(raw);
}

void set_integer(lua_State* state, int table, const char* key,
                 lua_Integer value) {
    table = lua_absindex(state, table);
    lua_pushinteger(state, value);
    lua_setfield(state, table, key);
}

void set_boolean(lua_State* state, int table, const char* key, bool value) {
    table = lua_absindex(state, table);
    lua_pushboolean(state, value);
    lua_setfield(state, table, key);
}

bool finite_vector(const Vector3& value) {
    return std::isfinite(value.x) && std::isfinite(value.y) &&
           std::isfinite(value.z);
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

bool read_vector(lua_State* state, int index, Vector3& output) {
    if (lua_isnoneornil(state, index)) return false;
    luaL_checktype(state, index, LUA_TTABLE);
    const int table = lua_absindex(state, index);
    lua_getfield(state, table, "x");
    output.x = static_cast<float>(luaL_checknumber(state, -1));
    lua_pop(state, 1);
    lua_getfield(state, table, "y");
    output.y = static_cast<float>(luaL_checknumber(state, -1));
    lua_pop(state, 1);
    lua_getfield(state, table, "z");
    output.z = static_cast<float>(luaL_checknumber(state, -1));
    lua_pop(state, 1);
    if (!finite_vector(output)) {
        luaL_argerror(state, index, "entity vector components must be finite");
    }
    return true;
}

bool valid_entity_info(const EntityInfo& entity) {
    return entity.valid && entity.entity_index >= 0 &&
           entity.owner_index >= -1 && entity.parent_index >= -1 &&
           entity.classname[0] != '\0' && finite_vector(entity.position) &&
           finite_vector(entity.angles) && finite_vector(entity.velocity);
}

void push_entity(lua_State* state, const Services* root,
                 const EntityInfo& entity, bool include_relations);

void apply_entity(lua_State* state, int table, const Services* root,
                  const EntityInfo& entity, bool include_relations) {
    table = lua_absindex(state, table);
    set_boolean(state, table, "valid", entity.valid);
    set_boolean(state, table, "spawned", entity.spawned);
    set_integer(state, table, "entity_index", entity.entity_index);
    set_integer(state, table, "handle", entity.handle);
    set_integer(state, table, "health", entity.health);
    set_integer(state, table, "team", entity.team);
    set_integer(state, table, "owner_index", entity.owner_index);
    set_integer(state, table, "parent_index", entity.parent_index);
    lua_pushstring(state, entity.classname);
    lua_setfield(state, table, "classname");
    lua_pushstring(state, entity.name);
    lua_setfield(state, table, "name");
    push_vector(state, entity.position);
    lua_setfield(state, table, "position");
    push_vector(state, entity.angles);
    lua_setfield(state, table, "angles");
    push_vector(state, entity.velocity);
    lua_setfield(state, table, "velocity");

    if (!include_relations) return;
    const auto* api = luacs::resolve_world_services(root);
    EntityInfo related;
    char error[256]{};
    if (entity.owner_index >= 0 && api && api->entity_get &&
        api->entity_get(api->context, entity.owner_index, &related, error,
                        sizeof(error)) &&
        valid_entity_info(related)) {
        push_entity(state, root, related, false);
        lua_setfield(state, table, "owner");
    } else {
        lua_pushnil(state);
        lua_setfield(state, table, "owner");
    }
    error[0] = '\0';
    if (entity.parent_index >= 0 && api && api->entity_get &&
        api->entity_get(api->context, entity.parent_index, &related, error,
                        sizeof(error)) &&
        valid_entity_info(related)) {
        push_entity(state, root, related, false);
        lua_setfield(state, table, "parent");
    } else {
        lua_pushnil(state);
        lua_setfield(state, table, "parent");
    }
}

void push_entity(lua_State* state, const Services* root,
                 const EntityInfo& entity, bool include_relations) {
    lua_createtable(state, 0, 18);
    apply_entity(state, -1, root, entity, include_relations);
    luaL_getmetatable(state, kEntityMeta);
    lua_setmetatable(state, -2);
}

bool fetch_entity(lua_State* state, int entity_index, EntityInfo& output,
                  char* error, std::size_t error_size) {
    const auto* api = world(state);
    if (!api || !api->entity_get ||
        !api->entity_get(api->context, entity_index, &output, error,
                         error_size)) {
        return false;
    }
    if (!valid_entity_info(output)) {
        if (error && error_size) {
            std::snprintf(error, error_size, "%s",
                          "Source 2 returned invalid entity state");
        }
        return false;
    }
    return true;
}

int get(lua_State* state) {
    const int index = entity_index_from(state, 1);
    EntityInfo entity;
    char error[256]{};
    if (!fetch_entity(state, index, entity, error, sizeof(error))) {
        if (!error[0]) {
            lua_pushnil(state);
            return 1;
        }
        return fail(state, error);
    }
    push_entity(state, services(state), entity, true);
    return 1;
}

int refresh(lua_State* state) {
    const int index = entity_index_from(state, 1);
    EntityInfo entity;
    char error[256]{};
    if (!fetch_entity(state, index, entity, error, sizeof(error))) {
        return fail(state, error);
    }
    if (lua_istable(state, 1)) {
        apply_entity(state, 1, services(state), entity, true);
        lua_pushvalue(state, 1);
    } else {
        push_entity(state, services(state), entity, true);
    }
    return 1;
}

int is_valid(lua_State* state) {
    const int index = entity_index_from(state, 1);
    EntityInfo entity;
    char error[64]{};
    lua_pushboolean(state,
                    fetch_entity(state, index, entity, error, sizeof(error)));
    return 1;
}

int find_many(lua_State* state, bool by_name) {
    const auto* api = world(state);
    const char* pattern = luaL_optstring(state, 1, "*");
    if (!api || !api->entity_count || !api->entity_at) {
        return fail(state, "entity discovery service is unavailable");
    }
    char error[256]{};
    const std::size_t count =
        api->entity_count(api->context, pattern, by_name, error, sizeof(error));
    if (error[0]) return fail(state, error);

    const int capacity = luacs::lua_checked::checked_table_capacity(
        state, count, "entity result is too large for a Lua table");
    lua_createtable(state, capacity, 0);
    lua_Integer output_index = 1;
    for (std::size_t index = 0; index < count; ++index) {
        EntityInfo entity;
        error[0] = '\0';
        if (!api->entity_at(api->context, pattern, by_name, index, &entity,
                            error, sizeof(error))) {
            return fail(state, error);
        }
        if (!valid_entity_info(entity)) {
            return fail(state, "Source 2 returned invalid entity state");
        }
        push_entity(state, services(state), entity, true);
        lua_seti(state, -2, output_index++);
    }
    return 1;
}

int find_by_classname(lua_State* state) { return find_many(state, false); }
int find_all_by_name(lua_State* state) { return find_many(state, true); }
int all(lua_State* state) {
    lua_settop(state, 0);
    lua_pushliteral(state, "*");
    return find_many(state, false);
}

int find_by_name(lua_State* state) {
    const auto* api = world(state);
    const char* pattern = luaL_checkstring(state, 1);
    if (!api || !api->entity_count || !api->entity_at) {
        return fail(state, "entity discovery service is unavailable");
    }
    char error[256]{};
    const std::size_t count =
        api->entity_count(api->context, pattern, true, error, sizeof(error));
    if (error[0]) return fail(state, error);
    if (count == 0) {
        lua_pushnil(state);
        return 1;
    }
    EntityInfo entity;
    if (!api->entity_at(api->context, pattern, true, 0, &entity, error,
                        sizeof(error))) {
        return fail(state, error);
    }
    if (!valid_entity_info(entity)) {
        return fail(state, "Source 2 returned invalid entity state");
    }
    push_entity(state, services(state), entity, true);
    return 1;
}

int count_impl(lua_State* state, bool by_name) {
    const auto* api = world(state);
    const char* pattern = luaL_optstring(state, 1, "*");
    char error[256]{};
    if (!api || !api->entity_count) {
        return fail(state, "entity discovery service is unavailable");
    }
    const std::size_t count =
        api->entity_count(api->context, pattern, by_name, error, sizeof(error));
    if (error[0]) return fail(state, error);
    lua_pushinteger(state, static_cast<lua_Integer>(count));
    return 1;
}

int count_by_classname(lua_State* state) { return count_impl(state, false); }
int count_by_name(lua_State* state) { return count_impl(state, true); }

void rollback_created(const WorldServices* api, int entity_index) {
    if (!api || !api->entity_remove || entity_index < 0) return;
    char ignored[256]{};
    api->entity_remove(api->context, entity_index, ignored, sizeof(ignored));
}

int create(lua_State* state) {
    const auto* api = world(state);
    std::size_t classname_size = 0;
    const char* classname_raw = luaL_checklstring(state, 1, &classname_size);
    const std::string classname(classname_raw ? classname_raw : "",
                                classname_size);
    if (classname.empty()) {
        return luaL_argerror(state, 1, "classname cannot be empty");
    }
    if (classname.find('\0') != std::string::npos) {
        return luaL_argerror(state, 1, "classname cannot contain NUL bytes");
    }
    if (!api || !api->entity_create || !api->entity_remove) {
        return fail(state, "entity creation/removal service is unavailable");
    }

    EntityInfo entity;
    char error[256]{};
    if (!api->entity_create(api->context, classname.c_str(), &entity, error,
                            sizeof(error))) {
        return fail(state,
                    error[0] ? error : "entity creation service is unavailable");
    }
    if (!valid_entity_info(entity)) {
        rollback_created(api, entity.entity_index);
        return fail(state, "Source 2 returned invalid created entity state");
    }

    if (lua_istable(state, 2)) {
        const int options = lua_absindex(state, 2);
        Vector3 position{};
        Vector3 angles{};
        Vector3 velocity{};
        lua_getfield(state, options, "position");
        const bool has_position = read_vector(state, -1, position);
        lua_pop(state, 1);
        lua_getfield(state, options, "angles");
        const bool has_angles = read_vector(state, -1, angles);
        lua_pop(state, 1);
        lua_getfield(state, options, "velocity");
        const bool has_velocity = read_vector(state, -1, velocity);
        lua_pop(state, 1);

        if (has_position || has_angles || has_velocity) {
            if (!api->entity_teleport ||
                !api->entity_teleport(api->context, entity.entity_index,
                                      has_position ? &position : nullptr,
                                      has_angles ? &angles : nullptr,
                                      has_velocity ? &velocity : nullptr, error,
                                      sizeof(error))) {
                const std::string message =
                    error[0] ? error : "entity teleport service is unavailable";
                rollback_created(api, entity.entity_index);
                return fail(state, message.c_str());
            }
        }

        bool should_spawn = false;
        lua_getfield(state, options, "spawn");
        if (!lua_isnil(state, -1)) {
            luaL_checktype(state, -1, LUA_TBOOLEAN);
            should_spawn = lua_toboolean(state, -1) != 0;
        }
        lua_pop(state, 1);
        if (should_spawn) {
            if (!api->entity_spawn ||
                !api->entity_spawn(api->context, entity.entity_index, error,
                                   sizeof(error))) {
                const std::string message =
                    error[0] ? error : "entity spawn service is unavailable";
                rollback_created(api, entity.entity_index);
                return fail(state, message.c_str());
            }
        }

        if (!fetch_entity(state, entity.entity_index, entity, error,
                          sizeof(error))) {
            const std::string message =
                error[0] ? error : "created entity could not be refreshed";
            rollback_created(api, entity.entity_index);
            return fail(state, message.c_str());
        }
    }

    push_entity(state, services(state), entity, true);
    return 1;
}

int spawn(lua_State* state) {
    const auto* api = world(state);
    const int index = entity_index_from(state, 1);
    char error[256]{};
    if (!api || !api->entity_spawn ||
        !api->entity_spawn(api->context, index, error, sizeof(error))) {
        return fail(state,
                    error[0] ? error : "entity spawn service is unavailable");
    }
    if (lua_istable(state, 1)) set_boolean(state, 1, "spawned", true);
    lua_pushboolean(state, true);
    return 1;
}

int remove_entity(lua_State* state) {
    const auto* api = world(state);
    const int index = entity_index_from(state, 1);
    char error[256]{};
    if (!api || !api->entity_remove ||
        !api->entity_remove(api->context, index, error, sizeof(error))) {
        return fail(state,
                    error[0] ? error : "entity removal service is unavailable");
    }
    if (lua_istable(state, 1)) set_boolean(state, 1, "valid", false);
    lua_pushboolean(state, true);
    return 1;
}

int teleport_impl(lua_State* state, const Vector3* position,
                  const Vector3* angles, const Vector3* velocity) {
    const auto* api = world(state);
    const int index = entity_index_from(state, 1);
    char error[256]{};
    if (!api || !api->entity_teleport ||
        !api->entity_teleport(api->context, index, position, angles, velocity,
                              error, sizeof(error))) {
        return fail(state,
                    error[0] ? error : "entity teleport service is unavailable");
    }
    if (lua_istable(state, 1)) {
        if (position) {
            push_vector(state, *position);
            lua_setfield(state, 1, "position");
        }
        if (angles) {
            push_vector(state, *angles);
            lua_setfield(state, 1, "angles");
        }
        if (velocity) {
            push_vector(state, *velocity);
            lua_setfield(state, 1, "velocity");
        }
    }
    lua_pushboolean(state, true);
    return 1;
}

int teleport(lua_State* state) {
    Vector3 position{};
    Vector3 angles{};
    Vector3 velocity{};
    const bool has_position = read_vector(state, 2, position);
    const bool has_angles = read_vector(state, 3, angles);
    const bool has_velocity = read_vector(state, 4, velocity);
    if (!has_position && !has_angles && !has_velocity) {
        return luaL_error(state,
                          "teleport requires position, angles, or velocity");
    }
    return teleport_impl(state, has_position ? &position : nullptr,
                         has_angles ? &angles : nullptr,
                         has_velocity ? &velocity : nullptr);
}

int set_position(lua_State* state) {
    Vector3 position{};
    read_vector(state, 2, position);
    return teleport_impl(state, &position, nullptr, nullptr);
}

int set_angles(lua_State* state) {
    Vector3 angles{};
    read_vector(state, 2, angles);
    return teleport_impl(state, nullptr, &angles, nullptr);
}

int set_velocity(lua_State* state) {
    Vector3 velocity{};
    read_vector(state, 2, velocity);
    return teleport_impl(state, nullptr, nullptr, &velocity);
}

int set_owner(lua_State* state) {
    const auto* api = world(state);
    const int index = entity_index_from(state, 1);
    const int owner = entity_index_from(state, 2, true);
    char error[256]{};
    if (!api || !api->entity_set_owner ||
        !api->entity_set_owner(api->context, index, owner, error,
                               sizeof(error))) {
        return fail(state,
                    error[0] ? error : "entity owner service is unavailable");
    }
    if (lua_istable(state, 1)) {
        set_integer(state, 1, "owner_index", owner);
        if (owner >= 0 && lua_istable(state, 2)) {
            lua_pushvalue(state, 2);
            lua_setfield(state, 1, "owner");
        } else {
            lua_pushnil(state);
            lua_setfield(state, 1, "owner");
        }
    }
    lua_pushboolean(state, true);
    return 1;
}

int set_parent(lua_State* state) {
    const auto* api = world(state);
    const int index = entity_index_from(state, 1);
    const int parent = entity_index_from(state, 2, true);
    char error[256]{};
    if (!api || !api->entity_set_parent ||
        !api->entity_set_parent(api->context, index, parent, error,
                                sizeof(error))) {
        return fail(state,
                    error[0] ? error : "entity parent service is unavailable");
    }
    if (lua_istable(state, 1)) {
        set_integer(state, 1, "parent_index", parent);
        if (parent >= 0 && lua_istable(state, 2)) {
            lua_pushvalue(state, 2);
            lua_setfield(state, 1, "parent");
        } else {
            lua_pushnil(state);
            lua_setfield(state, 1, "parent");
        }
    }
    lua_pushboolean(state, true);
    return 1;
}

int clear_parent(lua_State* state) {
    lua_settop(state, 1);
    lua_pushnil(state);
    return set_parent(state);
}

std::string checked_text(lua_State* state, int index, const char* label,
                         bool allow_empty) {
    std::size_t size = 0;
    const char* raw = luaL_checklstring(state, index, &size);
    std::string value(raw ? raw : "", size);
    if (!allow_empty && value.empty()) {
        std::string message(label);
        message += " cannot be empty";
        luaL_argerror(state, index, message.c_str());
    }
    if (value.find('\0') != std::string::npos) {
        std::string message(label);
        message += " cannot contain NUL bytes";
        luaL_argerror(state, index, message.c_str());
    }
    return value;
}

int input(lua_State* state) {
    const auto* api = world(state);
    const int index = entity_index_from(state, 1);
    const std::string name = checked_text(state, 2, "input name", false);
    const std::string value = lua_isnoneornil(state, 3)
                                  ? std::string()
                                  : checked_text(state, 3, "input value", true);
    const int activator = entity_index_from(state, 4, true);
    const int caller = entity_index_from(state, 5, true);
    const float delay = lua_isnoneornil(state, 6)
                            ? 0.0f
                            : luacs::lua_checked::finite_float(
                                  state, 6, "input delay must be finite");
    if (delay < 0.0f) {
        return luaL_argerror(state, 6, "delay cannot be negative");
    }
    char error[256]{};
    if (!api || !api->entity_accept_input ||
        !api->entity_accept_input(api->context, index, name.c_str(),
                                  value.c_str(), activator, caller, delay,
                                  error, sizeof(error))) {
        return fail(state,
                    error[0] ? error : "entity input service is unavailable");
    }
    lua_pushboolean(state, true);
    return 1;
}

int entity_tostring(lua_State* state) {
    lua_getfield(state, 1, "classname");
    const char* classname = lua_tostring(state, -1);
    const std::string stable_classname = classname ? classname : "";
    lua_pop(state, 1);
    lua_getfield(state, 1, "entity_index");
    const lua_Integer index = luaL_checkinteger(state, -1);
    lua_pop(state, 1);
    lua_pushfstring(state, "Entity(%I, %s)", index,
                    stable_classname.c_str());
    return 1;
}

void add_function(lua_State* state, const Services* api, const char* name,
                  lua_CFunction function) {
    lua_pushlightuserdata(state, const_cast<Services*>(api));
    lua_pushcclosure(state, function, 1);
    lua_setfield(state, -2, name);
}

} // namespace

LUACS_MODULE_EXPORT int LuaCS_OpenModule(lua_State* state,
                                         const Services* api) {
    if (!api || api->abi_version != luacs::kModuleAbiVersion) {
        return luaL_error(state, "LuaCS entities ABI mismatch");
    }
    if (!luacs::resolve_world_services(api)) {
        return luaL_error(state, "LuaCS world services are unavailable");
    }

    if (luaL_newmetatable(state, kEntityMeta)) {
        lua_newtable(state);
        add_function(state, api, "refresh", &refresh);
        add_function(state, api, "is_valid", &is_valid);
        add_function(state, api, "spawn", &spawn);
        add_function(state, api, "remove", &remove_entity);
        add_function(state, api, "teleport", &teleport);
        add_function(state, api, "set_position", &set_position);
        add_function(state, api, "set_angles", &set_angles);
        add_function(state, api, "set_velocity", &set_velocity);
        add_function(state, api, "set_owner", &set_owner);
        add_function(state, api, "set_parent", &set_parent);
        add_function(state, api, "clear_parent", &clear_parent);
        add_function(state, api, "input", &input);
        lua_setfield(state, -2, "__index");
        add_function(state, api, "__tostring", &entity_tostring);
    }
    lua_pop(state, 1);

    lua_createtable(state, 0, 24);
    add_function(state, api, "get", &get);
    add_function(state, api, "refresh", &refresh);
    add_function(state, api, "is_valid", &is_valid);
    add_function(state, api, "all", &all);
    add_function(state, api, "find_by_classname", &find_by_classname);
    add_function(state, api, "find_by_name", &find_by_name);
    add_function(state, api, "find_all_by_name", &find_all_by_name);
    add_function(state, api, "count_by_classname", &count_by_classname);
    add_function(state, api, "count_by_name", &count_by_name);
    add_function(state, api, "create", &create);
    add_function(state, api, "spawn", &spawn);
    add_function(state, api, "remove", &remove_entity);
    add_function(state, api, "teleport", &teleport);
    add_function(state, api, "set_owner", &set_owner);
    add_function(state, api, "set_parent", &set_parent);
    add_function(state, api, "input", &input);
    return 1;
}
