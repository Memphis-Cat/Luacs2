#include "luacs/lua_checked.h"
#include "luacs/module_api.h"
#include "luacs/world_module.h"

extern "C" {
#include "lauxlib.h"
}

#include <cmath>
#include <cstdint>
#include <limits>
#include <string>

namespace {

using luacs::PlayerInfo;
using luacs::PlayerState;
using luacs::Services;
using luacs::SoundInfo;
using luacs::SoundRequest;
using luacs::Vector3;
using luacs::WorldServices;

inline constexpr const char* kSoundMeta = "LuaCS.Sound";

const Services* services(lua_State* state) {
    return static_cast<const Services*>(
        lua_touserdata(state, lua_upvalueindex(1)));
}

const WorldServices* world(lua_State* state) {
    return luacs::resolve_world_services(services(state));
}

int fail(lua_State* state, const char* error) {
    lua_pushnil(state);
    lua_pushstring(state, error && *error ? error : "sound operation failed");
    return 2;
}

std::uint64_t all_players_mask(const Services* api) {
    if (!api || !api->player_count || !api->player_at) return 0;
    std::uint64_t mask = 0;
    const std::size_t count = api->player_count(api->context);
    for (std::size_t index = 0; index < count; ++index) {
        PlayerInfo player;
        if (!api->player_at(api->context, index, &player)) continue;
        if (player.connected && player.slot >= 0 && player.slot < 64) {
            mask |= std::uint64_t{1} << player.slot;
        }
    }
    return mask;
}

int slot_from_table(lua_State* state, int index) {
    index = lua_absindex(state, index);
    lua_getfield(state, index, "slot");
    if (lua_isnil(state, -1)) {
        lua_pop(state, 1);
        return -1;
    }
    if (!lua_isinteger(state, -1)) {
        lua_pop(state, 1);
        return luaL_argerror(state, index,
                             "player slot field must be an integer");
    }
    const lua_Integer raw = lua_tointeger(state, -1);
    lua_pop(state, 1);
    if (raw < 0 || raw >= 64) {
        return luaL_argerror(state, index,
                             "player slot must be between 0 and 63");
    }
    return static_cast<int>(raw);
}

bool add_recipient(lua_State* state, int index, std::uint64_t& mask) {
    if (lua_isinteger(state, index)) {
        const int slot = luacs::lua_checked::checked_slot(state, index);
        mask |= std::uint64_t{1} << slot;
        return true;
    }
    if (lua_istable(state, index)) {
        const int slot = slot_from_table(state, index);
        if (slot >= 0) {
            mask |= std::uint64_t{1} << slot;
            return true;
        }
    }
    luaL_argerror(state, index,
                  "recipient must be a player, slot, or array of players/slots");
    return false;
}

std::uint64_t recipient_mask(lua_State* state, int index,
                             const Services* api,
                             std::uint64_t default_mask = 0) {
    if (lua_isnoneornil(state, index)) {
        return default_mask ? default_mask : all_players_mask(api);
    }
    if (lua_isinteger(state, index)) {
        std::uint64_t mask = 0;
        add_recipient(state, index, mask);
        return mask;
    }
    luaL_checktype(state, index, LUA_TTABLE);
    const int direct_slot = slot_from_table(state, index);
    if (direct_slot >= 0) {
        return std::uint64_t{1} << direct_slot;
    }

    std::uint64_t mask = 0;
    const lua_Integer length =
        static_cast<lua_Integer>(lua_rawlen(state, index));
    for (lua_Integer item = 1; item <= length; ++item) {
        lua_geti(state, index, item);
        add_recipient(state, -1, mask);
        lua_pop(state, 1);
    }
    return mask;
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
    if (!std::isfinite(output.x) || !std::isfinite(output.y) ||
        !std::isfinite(output.z)) {
        luaL_argerror(state, index, "sound origin components must be finite");
    }
    return true;
}

int checked_entity_value(lua_State* state, int argument, lua_Integer raw) {
    if (raw < 0 || raw > std::numeric_limits<int>::max()) {
        return luaL_argerror(
            state, argument,
            "sound source entity index must be a non-negative 32-bit integer");
    }
    return static_cast<int>(raw);
}

int source_index(lua_State* state, int options_index, const Services* api) {
    if (!lua_istable(state, options_index)) return 0;
    options_index = lua_absindex(state, options_index);
    lua_getfield(state, options_index, "source");
    if (lua_isnoneornil(state, -1)) {
        lua_pop(state, 1);
        return 0;
    }
    if (lua_isinteger(state, -1)) {
        const lua_Integer raw = lua_tointeger(state, -1);
        lua_pop(state, 1);
        return checked_entity_value(state, options_index, raw);
    }
    luaL_checktype(state, -1, LUA_TTABLE);
    const int source_table = lua_absindex(state, -1);
    lua_getfield(state, source_table, "entity_index");
    if (lua_isinteger(state, -1)) {
        const lua_Integer raw = lua_tointeger(state, -1);
        lua_pop(state, 2);
        return checked_entity_value(state, options_index, raw);
    }
    lua_pop(state, 1);
    lua_getfield(state, source_table, "pawn_index");
    if (lua_isinteger(state, -1)) {
        const lua_Integer raw = lua_tointeger(state, -1);
        lua_pop(state, 2);
        return checked_entity_value(state, options_index, raw);
    }
    lua_pop(state, 1);
    const int slot = slot_from_table(state, source_table);
    lua_pop(state, 1);
    if (slot >= 0 && api && api->player_state) {
        PlayerState player;
        char error[128]{};
        if (api->player_state(api->context, slot, &player, error,
                              sizeof(error)) &&
            player.has_pawn && player.pawn_index >= 0) {
            return player.pawn_index;
        }
        luaL_argerror(state, options_index,
                      "sound source player has no live pawn");
    }
    return luaL_argerror(
        state, options_index,
        "sound source must be an entity index, entity, or player table");
}

void parse_options(lua_State* state, int index, const Services* api,
                   SoundRequest& request) {
    if (lua_isnoneornil(state, index)) return;
    luaL_checktype(state, index, LUA_TTABLE);
    index = lua_absindex(state, index);
    request.source_entity_index = source_index(state, index, api);

    lua_getfield(state, index, "origin");
    request.has_origin = read_vector(state, -1, request.origin);
    lua_pop(state, 1);

    lua_getfield(state, index, "volume");
    if (!lua_isnil(state, -1)) {
        request.volume = static_cast<float>(luaL_checknumber(state, -1));
        if (!std::isfinite(request.volume) || request.volume < 0.0f ||
            request.volume > 10.0f) {
            lua_pop(state, 1);
            luaL_argerror(state, index,
                          "volume must be finite and between 0 and 10");
        }
    }
    lua_pop(state, 1);

    lua_getfield(state, index, "pitch");
    if (!lua_isnil(state, -1)) {
        const lua_Integer raw = luaL_checkinteger(state, -1);
        if (raw < 1 || raw > 255) {
            lua_pop(state, 1);
            luaL_argerror(state, index, "pitch must be between 1 and 255");
        }
        request.pitch = static_cast<int>(raw);
    }
    lua_pop(state, 1);

    lua_getfield(state, index, "delay");
    if (!lua_isnil(state, -1)) {
        request.delay = static_cast<float>(luaL_checknumber(state, -1));
        if (!std::isfinite(request.delay) || request.delay < 0.0f ||
            request.delay > 3600.0f) {
            lua_pop(state, 1);
            luaL_argerror(
                state, index,
                "delay must be finite and between 0 and 3600 seconds");
        }
    }
    lua_pop(state, 1);

    lua_getfield(state, index, "channel");
    if (!lua_isnil(state, -1)) {
        const lua_Integer raw = luaL_checkinteger(state, -1);
        if (raw < std::numeric_limits<int>::min() ||
            raw > std::numeric_limits<int>::max()) {
            lua_pop(state, 1);
            luaL_argerror(state, index, "channel must fit a 32-bit integer");
        }
        request.channel = static_cast<int>(raw);
    }
    lua_pop(state, 1);

    lua_getfield(state, index, "reliable");
    if (!lua_isnil(state, -1)) {
        luaL_checktype(state, -1, LUA_TBOOLEAN);
        request.reliable = lua_toboolean(state, -1) != 0;
    }
    lua_pop(state, 1);
}

bool valid_sound(const SoundInfo& sound) {
    return sound.valid && sound.guid != 0 && sound.source_entity_index >= 0 &&
           sound.recipients_mask != 0 && sound.name[0] != '\0';
}

void push_sound(lua_State* state, const SoundInfo& sound) {
    lua_createtable(state, 0, 9);
    lua_pushboolean(state, sound.valid);
    lua_setfield(state, -2, "valid");
    lua_pushinteger(state, sound.guid);
    lua_setfield(state, -2, "guid");
    lua_pushinteger(state, sound.stack_hash);
    lua_setfield(state, -2, "stack_hash");
    lua_pushinteger(state, sound.source_entity_index);
    lua_setfield(state, -2, "source_entity_index");
    luacs::lua_checked::push_u64_exact(state, sound.recipients_mask);
    lua_setfield(state, -2, "recipients_mask");
    lua_pushinteger(state, sound.channel);
    lua_setfield(state, -2, "channel");
    lua_pushstring(state, sound.name);
    lua_setfield(state, -2, "name");
    luaL_getmetatable(state, kSoundMeta);
    lua_setmetatable(state, -2);
}

int emit_impl(lua_State* state, bool everyone) {
    const auto* root = services(state);
    const auto* api = world(state);
    const int sound_index = everyone ? 1 : 2;
    const int options_index = everyone ? 2 : 3;
    std::size_t name_size = 0;
    const char* sound_name = luaL_checklstring(state, sound_index, &name_size);
    if (!sound_name || name_size == 0) {
        return luaL_argerror(state, sound_index, "sound name cannot be empty");
    }
    const std::string sound_name_storage(sound_name, name_size);
    if (sound_name_storage.find('\0') != std::string::npos) {
        return luaL_argerror(state, sound_index,
                             "sound name cannot contain NUL bytes");
    }

    SoundRequest request;
    request.recipients_mask = everyone
                                  ? all_players_mask(root)
                                  : recipient_mask(state, 1, root);
    if (request.recipients_mask == 0) {
        return fail(state, "recipient filter contains no connected players");
    }
    parse_options(state, options_index, root, request);

    SoundInfo output;
    char error[512]{};
    if (!api || !api->sound_emit ||
        !api->sound_emit(api->context, sound_name_storage.c_str(), &request,
                         &output, error, sizeof(error))) {
        return fail(state,
                    error[0] ? error : "sound emission service is unavailable");
    }
    if (!valid_sound(output)) {
        return fail(state, "Source 2 returned invalid sound state");
    }
    push_sound(state, output);
    return 1;
}

int emit(lua_State* state) { return emit_impl(state, false); }
int emit_all(lua_State* state) { return emit_impl(state, true); }
int emit_to(lua_State* state) { return emit_impl(state, false); }

bool sound_fields(lua_State* state, int index, std::uint32_t& guid,
                  std::uint64_t& recipients, int& channel) {
    if (!lua_istable(state, index)) return false;
    index = lua_absindex(state, index);
    lua_getfield(state, index, "guid");
    if (!lua_isinteger(state, -1)) {
        lua_pop(state, 1);
        return false;
    }
    const lua_Integer raw_guid = lua_tointeger(state, -1);
    lua_pop(state, 1);
    if (raw_guid <= 0 ||
        static_cast<std::uint64_t>(raw_guid) >
            std::numeric_limits<std::uint32_t>::max()) {
        luaL_argerror(state, index, "sound object contains an invalid GUID");
    }
    guid = static_cast<std::uint32_t>(raw_guid);

    lua_getfield(state, index, "recipients_mask");
    recipients = luacs::lua_checked::read_u64_exact(
        state, -1, "sound recipients mask");
    lua_pop(state, 1);

    lua_getfield(state, index, "channel");
    const lua_Integer raw_channel = luaL_checkinteger(state, -1);
    lua_pop(state, 1);
    if (raw_channel < std::numeric_limits<int>::min() ||
        raw_channel > std::numeric_limits<int>::max()) {
        luaL_argerror(state, index, "sound object contains an invalid channel");
    }
    channel = static_cast<int>(raw_channel);
    return true;
}

int stop(lua_State* state) {
    const auto* root = services(state);
    const auto* api = world(state);
    std::uint32_t guid = 0;
    std::uint64_t original_recipients = 0;
    int channel = 0;
    const bool object =
        sound_fields(state, 1, guid, original_recipients, channel);
    if (!object) {
        guid = luacs::lua_checked::checked_u32(
            state, 1, "sound GUID must fit an unsigned 32-bit integer");
        if (guid == 0) return luaL_argerror(state, 1, "sound GUID must be non-zero");
    }
    const std::uint64_t recipients = recipient_mask(
        state, 2, root,
        original_recipients ? original_recipients : all_players_mask(root));
    if (recipients == 0) return fail(state, "sound recipient filter is empty");
    const bool reliable =
        luacs::lua_checked::optional_boolean(state, 3, true);
    char error[256]{};
    if (!api || !api->sound_stop ||
        !api->sound_stop(api->context, guid, recipients, reliable, error,
                         sizeof(error))) {
        return fail(state,
                    error[0] ? error : "sound stop service is unavailable");
    }
    if (object) {
        lua_pushboolean(state, false);
        lua_setfield(state, 1, "valid");
    }
    lua_pushboolean(state, true);
    return 1;
}

int stop_channel(lua_State* state) {
    const auto* root = services(state);
    const auto* api = world(state);
    const int channel = luacs::lua_checked::checked_int(
        state, 1, std::numeric_limits<int>::min(),
        std::numeric_limits<int>::max(), "channel must fit a 32-bit integer");
    const std::uint64_t recipients =
        recipient_mask(state, 2, root, all_players_mask(root));
    if (recipients == 0) return fail(state, "sound recipient filter is empty");
    const bool reliable =
        luacs::lua_checked::optional_boolean(state, 3, true);
    char error[256]{};
    if (!api || !api->sound_stop_channel) {
        return fail(state, "sound channel service is unavailable");
    }
    const std::size_t stopped = api->sound_stop_channel(
        api->context, channel, recipients, reliable, error, sizeof(error));
    if (error[0]) return fail(state, error);
    lua_pushinteger(state, static_cast<lua_Integer>(stopped));
    return 1;
}

int sound_tostring(lua_State* state) {
    lua_getfield(state, 1, "name");
    const char* name = lua_tostring(state, -1);
    const std::string stable_name = name ? name : "";
    lua_pop(state, 1);
    lua_getfield(state, 1, "guid");
    const lua_Integer guid = luaL_checkinteger(state, -1);
    lua_pop(state, 1);
    lua_pushfstring(state, "Sound(%I, %s)", guid, stable_name.c_str());
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
        return luaL_error(state, "LuaCS sounds ABI mismatch");
    }
    if (!luacs::resolve_world_services(api)) {
        return luaL_error(state, "LuaCS world services are unavailable");
    }

    if (luaL_newmetatable(state, kSoundMeta)) {
        lua_newtable(state);
        add_function(state, api, "stop", &stop);
        lua_setfield(state, -2, "__index");
        add_function(state, api, "__tostring", &sound_tostring);
    }
    lua_pop(state, 1);

    lua_createtable(state, 0, 10);
    add_function(state, api, "emit", &emit);
    add_function(state, api, "emit_to", &emit_to);
    add_function(state, api, "emit_all", &emit_all);
    add_function(state, api, "stop", &stop);
    add_function(state, api, "stop_channel", &stop_channel);
    return 1;
}
