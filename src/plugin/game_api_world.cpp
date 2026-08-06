#include "game_api_internal.h"
#include "luacs/world_api.h"

#include <igameeventsystem.h>
#include <irecipientfilter.h>
#include <networksystem/inetworkmessages.h>
#include <tier1/convar.h>
#include <variant.h>

#include "gameevents.pb.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <optional>
#include <regex>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

using namespace luacs;
using namespace luacs_game_internal;

std::optional<std::string> windows_value(const std::string& json,
                                         std::string_view entry_name) {
    std::size_t position = json.find("\"" + std::string(entry_name) + "\"");
    if (position == std::string::npos) position = json.find(entry_name);
    if (position == std::string::npos) return std::nullopt;
    const auto maximum = std::min(json.size(), position + 8192);
    const std::string block = json.substr(position, maximum - position);
    const std::regex expression(
        "\"windows\"\\s*:\\s*(?:\"([^\"]+)\"|([0-9]+))",
        std::regex::ECMAScript);
    std::smatch match;
    if (!std::regex_search(block, match, expression)) return std::nullopt;
    if (match[1].matched) return match[1].str();
    if (match[2].matched) return match[2].str();
    return std::nullopt;
}

std::optional<std::size_t> windows_number(const std::string& json,
                                          std::string_view entry_name) {
    const auto value = windows_value(json, entry_name);
    if (!value) return std::nullopt;
    try {
        return static_cast<std::size_t>(std::stoull(*value));
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<std::ptrdiff_t> find_schema_field(CSchemaClassInfo* class_info,
                                                std::string_view field_name) {
    if (!class_info) return std::nullopt;
    for (std::uint16_t index = 0; index < class_info->m_nFieldCount; ++index) {
        const auto& field = class_info->m_pFields[index];
        if (field.m_pszName && field_name == field.m_pszName) {
            return static_cast<std::ptrdiff_t>(field.m_nSingleInheritanceOffset);
        }
    }
    for (std::uint8_t index = 0; index < class_info->m_nBaseClassCount; ++index) {
        const auto& base = class_info->m_pBaseClasses[index];
        const auto inherited = find_schema_field(base.m_pClass, field_name);
        if (inherited) {
            return static_cast<std::ptrdiff_t>(base.m_nOffset) + *inherited;
        }
    }
    return std::nullopt;
}

bool resolve_field(CSchemaSystemTypeScope* scope, std::ptrdiff_t& output,
                   const char* class_name, const char* field_name,
                   std::string& error) {
    if (!scope) return false;
    const auto class_handle = scope->FindDeclaredClass(class_name);
    const auto field = find_schema_field(class_handle.Get(), field_name);
    if (!field) {
        error = std::string("Source 2 schema field was not found: ") +
                class_name + "::" + field_name;
        return false;
    }
    output = *field;
    return true;
}

std::filesystem::path luacs_root() {
    HMODULE module{};
    if (!GetModuleHandleExW(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(&luacs_root), &module)) {
        return {};
    }
    std::wstring buffer(32768, L'\0');
    const DWORD length =
        GetModuleFileNameW(module, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (!length || length >= buffer.size()) return {};
    buffer.resize(length);
    auto path = std::filesystem::path(buffer).parent_path();
    if (path.filename() == L"win64") path = path.parent_path();
    if (path.filename() == L"bin") path = path.parent_path();
    return path;
}

std::string gamedata_text(const std::filesystem::path& root) {
    const auto reference = root / "gamedata" / "reference";
    std::string text = read_file(reference / "official_windows_gamedata.json");
    const std::string combined =
        read_file(reference / "combined_namespaced_windows_reference.json");
    if (!combined.empty()) {
        text.push_back('\n');
        text += combined;
    }
    return text;
}

void copy_text(char* destination, std::size_t capacity,
               std::string_view value) {
    if (!destination || capacity == 0) return;
    std::snprintf(destination, capacity, "%.*s",
                  static_cast<int>(value.size()), value.data());
}

std::string lower(std::string_view value) {
    std::string result(value);
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char character) {
                       return static_cast<char>(std::tolower(character));
                   });
    return result;
}

bool wildcard_match(std::string_view pattern, std::string_view value) {
    const std::string p = lower(pattern);
    const std::string v = lower(value);
    std::size_t pi = 0, vi = 0, star = std::string::npos, retry = 0;
    while (vi < v.size()) {
        if (pi < p.size() && (p[pi] == '?' || p[pi] == v[vi])) {
            ++pi;
            ++vi;
        } else if (pi < p.size() && p[pi] == '*') {
            star = pi++;
            retry = vi;
        } else if (star != std::string::npos) {
            pi = star + 1;
            vi = ++retry;
        } else {
            return false;
        }
    }
    while (pi < p.size() && p[pi] == '*') ++pi;
    return pi == p.size();
}

class LuaCSRecipientFilter final : public IRecipientFilter {
public:
    explicit LuaCSRecipientFilter(std::uint64_t mask,
                                  bool reliable = true)
        : buffer_type_(reliable ? NetChannelBufType_t::BUF_RELIABLE
                                : NetChannelBufType_t::BUF_UNRELIABLE) {
        for (int slot = 0; slot < 64; ++slot) {
            if (mask & (std::uint64_t{1} << slot)) recipients_.Set(slot);
        }
    }

    NetChannelBufType_t GetNetworkBufType() const override {
        return buffer_type_;
    }
    bool IsInitMessage() const override { return false; }
    const CPlayerBitVec& GetRecipients() const override { return recipients_; }
    CPlayerSlot GetPredictedPlayerSlot() const override {
        return CPlayerSlot(-1);
    }

private:
    CPlayerBitVec recipients_{};
    NetChannelBufType_t buffer_type_{};
};

struct SoundGuid {
    std::uint32_t guid{};
    std::uint32_t stack_hash{};
};

#pragma pack(push, 1)
struct StartSoundEventInfo {
    SoundGuid sound{};
    std::int32_t flags{};
    std::uint64_t recipients{};
};
#pragma pack(pop)

struct EmitSoundParams {
    const char* sound_name{};
    Vector origin{};
    float volume{1.0f};
    float sound_time{0.0f};
    std::uint32_t padding_1{};
    std::uint32_t force_guid{};
    std::uint32_t padding_2{};
    std::int16_t pitch{100};
    std::uint8_t flags{};
    std::uint8_t padding_3[5]{};
};

class LuaCSWorldApi {
public:
    using CreateEntityFn = CEntityInstance*(__fastcall*)(const char*, int);
    using DispatchSpawnFn = void(__fastcall*)(CEntityInstance*, void*);
    using RemoveEntityFn = void(__fastcall*)(CEntityInstance*);
    using AcceptInputFn = void(__fastcall*)(CEntityInstance*, const char*,
                                             CEntityInstance*, CEntityInstance*,
                                             variant_t*, int, void*);
    using AddInputEventFn = void(__fastcall*)(CEntitySystem*, CEntityInstance*,
                                               const char*, CEntityInstance*,
                                               CEntityInstance*, variant_t*,
                                               float, void*, void*);
    using TeleportFn = void(__fastcall*)(CEntityInstance*, const Vector*,
                                         const QAngle*, const Vector*);
    using TerminateRoundFn = void(__fastcall*)(void*, float, int, void*,
                                               std::uint8_t);
    using EmitSoundFilterFn = StartSoundEventInfo(__fastcall*)(
        IRecipientFilter&, CEntityIndex, const EmitSoundParams&);

    struct ActiveSound {
        std::uint32_t guid{};
        std::uint32_t stack_hash{};
        int source_entity_index{};
        std::uint64_t recipients_mask{};
        int channel{};
        std::string name;
    };

    LuaCSWorldApi() {
        services.abi_version = kWorldServicesAbiVersion;
        services.context = this;
        services.team_get_score = &team_get_score_bridge;
        services.team_set_score = &team_set_score_bridge;
        services.round_state = &round_state_bridge;
        services.round_restart = &round_restart_bridge;
        services.round_terminate = &round_terminate_bridge;
        services.round_set_frozen = &round_set_frozen_bridge;
        services.entity_get = &entity_get_bridge;
        services.entity_count = &entity_count_bridge;
        services.entity_at = &entity_at_bridge;
        services.entity_create = &entity_create_bridge;
        services.entity_spawn = &entity_spawn_bridge;
        services.entity_remove = &entity_remove_bridge;
        services.entity_teleport = &entity_teleport_bridge;
        services.entity_set_owner = &entity_set_owner_bridge;
        services.entity_set_parent = &entity_set_parent_bridge;
        services.entity_accept_input = &entity_accept_input_bridge;
        services.sound_emit = &sound_emit_bridge;
        services.sound_stop = &sound_stop_bridge;
        services.sound_stop_channel = &sound_stop_channel_bridge;
    }

    bool initialize(std::string& error) {
        if (initialized_) return true;
        if (attempted_) {
            error = initialization_error_;
            return false;
        }
        attempted_ = true;

        const auto root = luacs_root();
        const std::string gamedata = gamedata_text(root);
        if (root.empty() || gamedata.empty()) {
            return init_fail("could not locate LuaCS or read its gamedata", error);
        }

        const auto game_entity_system =
            windows_number(gamedata, "GameEntitySystem");
        const auto teleport = windows_number(gamedata, "CBaseEntity_Teleport");
        const auto terminate =
            windows_value(gamedata, "CCSGameRules_TerminateRound");
        const auto create_entity =
            windows_value(gamedata, "UTIL_CreateEntityByName");
        const auto dispatch_spawn =
            windows_value(gamedata, "CBaseEntity_DispatchSpawn");
        const auto remove_entity = windows_value(gamedata, "UTIL_Remove");
        const auto accept_input =
            windows_value(gamedata, "CEntityInstance_AcceptInput");
        const auto add_input_event =
            windows_value(gamedata, "CEntitySystem_AddEntityIOEvent");
        const auto emit_sound =
            windows_value(gamedata, "CBaseEntity_EmitSoundFilter");
        if (!game_entity_system || !teleport || !terminate || !create_entity ||
            !dispatch_spawn || !remove_entity || !accept_input ||
            !add_input_event || !emit_sound) {
            return init_fail("gamedata is missing a teams, rounds, entities, or sounds entry", error);
        }

        const auto engine_factory = module_factory(L"engine2.dll");
        const auto schema_factory = module_factory(L"schemasystem.dll");
        const auto cvar_factory = module_factory(L"tier0.dll");
        if (!engine_factory || !schema_factory || !cvar_factory) {
            return init_fail("could not resolve required Source 2 factories", error);
        }

        game_resource_service_ =
            engine_factory("GameResourceServiceServerV001", nullptr);
        entity_system_offset_ = *game_entity_system;
        schema_system_ = static_cast<CSchemaSystem*>(
            schema_factory(SCHEMASYSTEM_INTERFACE_VERSION, nullptr));
        cvars_ = static_cast<ICvar*>(cvar_factory("VEngineCvar007", nullptr));
        game_event_system_ = static_cast<IGameEventSystem*>(
            engine_factory(GAMEEVENTSYSTEM_INTERFACE_VERSION, nullptr));
        network_messages_ = static_cast<INetworkMessages*>(
            engine_factory(NETWORKMESSAGES_INTERFACE_VERSION, nullptr));
        if (!game_resource_service_ || !schema_system_ || !cvars_ ||
            !game_event_system_ || !network_messages_) {
            return init_fail("could not acquire world-service Source 2 interfaces", error);
        }
        if (!g_pCVar) g_pCVar = cvars_;

        scope_ = schema_system_->FindTypeScopeForModule("server.dll");
        if (!scope_) return init_fail("server.dll schema scope is unavailable", error);

#define WORLD_FIELD(member, class_name, field_name)                           \
    if (!resolve_field(scope_, member, class_name, field_name, error)) {       \
        initialization_error_ = error;                                         \
        return false;                                                          \
    }
        WORLD_FIELD(health_offset_, "CBaseEntity", "m_iHealth");
        WORLD_FIELD(team_offset_, "CBaseEntity", "m_iTeamNum");
        WORLD_FIELD(velocity_offset_, "CBaseEntity", "m_vecAbsVelocity");
        WORLD_FIELD(body_offset_, "CBaseEntity", "m_CBodyComponent");
        WORLD_FIELD(owner_offset_, "CBaseEntity", "m_hOwnerEntity");
        WORLD_FIELD(scene_node_offset_, "CBodyComponent", "m_pSceneNode");
        WORLD_FIELD(origin_offset_, "CGameSceneNode", "m_vecAbsOrigin");
        WORLD_FIELD(rotation_offset_, "CGameSceneNode", "m_angAbsRotation");
        WORLD_FIELD(parent_node_offset_, "CGameSceneNode", "m_pParent");
        WORLD_FIELD(node_owner_offset_, "CGameSceneNode", "m_pOwner");
        WORLD_FIELD(team_score_offset_, "CTeam", "m_iScore");
        WORLD_FIELD(game_rules_pointer_offset_, "CCSGameRulesProxy",
                    "m_pGameRules");
        WORLD_FIELD(freeze_period_offset_, "CCSGameRules", "m_bFreezePeriod");
        WORLD_FIELD(total_rounds_offset_, "CCSGameRules", "m_totalRoundsPlayed");
        WORLD_FIELD(round_win_status_offset_, "CCSGameRules",
                    "m_iRoundWinStatus");
        WORLD_FIELD(round_win_reason_offset_, "CCSGameRules",
                    "m_eRoundWinReason");
        WORLD_FIELD(restart_time_offset_, "CCSGameRules",
                    "m_flRestartRoundTime");
#undef WORLD_FIELD

        teleport_index_ = *teleport;
        const HMODULE server = GetModuleHandleW(L"server.dll");
        if (!server) return init_fail("server.dll is not loaded", error);
        terminate_round_ = reinterpret_cast<TerminateRoundFn>(
            find_pattern(server, *terminate));
        create_entity_ = reinterpret_cast<CreateEntityFn>(
            find_pattern(server, *create_entity));
        dispatch_spawn_ = reinterpret_cast<DispatchSpawnFn>(
            find_pattern(server, *dispatch_spawn));
        remove_entity_ = reinterpret_cast<RemoveEntityFn>(
            find_pattern(server, *remove_entity));
        accept_input_ = reinterpret_cast<AcceptInputFn>(
            find_pattern(server, *accept_input));
        add_input_event_ = reinterpret_cast<AddInputEventFn>(
            find_pattern(server, *add_input_event));
        emit_sound_filter_ = reinterpret_cast<EmitSoundFilterFn>(
            find_pattern(server, *emit_sound));
        if (!terminate_round_ || !create_entity_ || !dispatch_spawn_ ||
            !remove_entity_ || !accept_input_ || !add_input_event_ ||
            !emit_sound_filter_) {
            return init_fail("could not resolve one or more world-service functions", error);
        }

        initialized_ = true;
        initialization_error_.clear();
        return true;
    }

    CGameEntitySystem* entity_system() const {
        if (entity_system_) return entity_system_;
        if (!game_resource_service_ || !entity_system_offset_) return nullptr;
        auto** pointer = reinterpret_cast<CGameEntitySystem**>(
            reinterpret_cast<std::uint8_t*>(game_resource_service_) +
            entity_system_offset_);
        if (pointer) entity_system_ = *pointer;
        return entity_system_;
    }

    CEntityInstance* entity(int index) const {
        auto* system = entity_system();
        if (!system || index < 0 || index >= MAX_TOTAL_ENTITIES) return nullptr;
        const int chunk_index = index / MAX_ENTITIES_IN_LIST;
        const int entry_index = index % MAX_ENTITIES_IN_LIST;
        CEntityIdentity* chunk =
            system->m_EntityList.m_pIdentityChunks[chunk_index];
        if (!chunk) return nullptr;
        CEntityIdentity* identity = &chunk[entry_index];
        if (!identity->m_pInstance ||
            identity->m_EHandle.GetEntryIndex() != index) {
            return nullptr;
        }
        return identity->m_pInstance;
    }

    bool entity_info(int index, EntityInfo& output, std::string& error) {
        if (!ready(error)) return false;
        CEntityInstance* instance = entity(index);
        if (!instance || !instance->m_pEntity) {
            error = "entity index " + std::to_string(index) + " is invalid";
            return false;
        }
        output = {};
        output.valid = true;
        output.entity_index = index;
        output.handle = static_cast<std::uint32_t>(
            instance->GetRefEHandle().ToInt());
        const auto identity_flags =
            static_cast<std::uint32_t>(instance->m_pEntity->m_flags);
        output.spawned =
            (identity_flags & (EF_SPAWN_IN_PROGRESS | EF_IS_PRE_SPAWN |
                               EF_DELETE_IN_PROGRESS | EF_MARKED_FOR_DELETE)) == 0;
        output.health = field<int>(instance, health_offset_);
        output.team = field<std::uint8_t>(instance, team_offset_);
        output.velocity = to_luacs(field<Vector>(instance, velocity_offset_));
        copy_text(output.classname, sizeof(output.classname),
                  instance->m_pEntity->GetClassname()
                      ? instance->m_pEntity->GetClassname()
                      : "");
        copy_text(output.name, sizeof(output.name),
                  instance->m_pEntity->m_name.String()
                      ? instance->m_pEntity->m_name.String()
                      : "");

        const auto owner = field<CEntityHandle>(instance, owner_offset_);
        output.owner_index = owner.IsValid() ? owner.GetEntryIndex() : -1;
        void* body = field<void*>(instance, body_offset_);
        if (body) {
            void* node = *reinterpret_cast<void**>(
                reinterpret_cast<std::uint8_t*>(body) + scene_node_offset_);
            if (node) {
                output.position = to_luacs(field<Vector>(node, origin_offset_));
                output.angles = to_luacs(field<QAngle>(node, rotation_offset_));
                void* parent = field<void*>(node, parent_node_offset_);
                if (parent) {
                    auto* parent_owner =
                        field<CEntityInstance*>(parent, node_owner_offset_);
                    if (parent_owner && parent_owner->m_pEntity) {
                        output.parent_index = parent_owner->GetRefEHandle().GetEntryIndex();
                    }
                }
            }
        }
        return true;
    }

    std::vector<int> find(std::string_view pattern, bool by_name,
                          std::string& error) {
        std::vector<int> result;
        if (!ready(error)) return result;
        if (pattern.empty()) pattern = "*";
        auto* system = entity_system();
        for (CEntityIdentity* identity =
                 system->m_EntityList.m_pFirstActiveEntity;
             identity; identity = identity->m_pNext) {
            if (!identity->m_pInstance) continue;
            const char* text = by_name ? identity->m_name.String()
                                       : identity->GetClassname();
            if (!text) text = "";
            if (wildcard_match(pattern, text)) {
                result.push_back(identity->GetEntityIndex().Get());
            }
        }
        return result;
    }

    CEntityInstance* team_entity(int team, std::string& error) {
        if (team < 1 || team > 3) {
            error = "team must be SPECTATOR, T, or CT";
            return nullptr;
        }
        auto matches = find("cs_team_manager", false, error);
        for (const int index : matches) {
            CEntityInstance* candidate = entity(index);
            if (candidate && field<std::uint8_t>(candidate, team_offset_) == team) {
                return candidate;
            }
        }
        error = "could not find the requested CS2 team manager";
        return nullptr;
    }

    void* game_rules(CEntityInstance*& proxy, std::string& error) {
        proxy = nullptr;
        auto matches = find("cs_gamerules", false, error);
        if (matches.empty()) {
            error = "CCSGameRulesProxy entity is unavailable";
            return nullptr;
        }
        proxy = entity(matches.front());
        if (!proxy) {
            error = "CCSGameRulesProxy entity is invalid";
            return nullptr;
        }
        void* rules = *reinterpret_cast<void**>(
            reinterpret_cast<std::uint8_t*>(proxy) +
            game_rules_pointer_offset_);
        if (!rules) error = "CCSGameRules pointer is null";
        return rules;
    }

    bool team_get_score(int team, int& output, std::string& error) {
        CEntityInstance* manager = team_entity(team, error);
        if (!manager) return false;
        output = field<int>(manager, team_score_offset_);
        return true;
    }

    bool team_set_score(int team, int score, std::string& error) {
        if (score < 0) {
            error = "team score cannot be negative";
            return false;
        }
        CEntityInstance* manager = team_entity(team, error);
        if (!manager) return false;
        field<int>(manager, team_score_offset_) = score;
        manager->NetworkStateChanged(NetworkStateChangedData(
            static_cast<std::uint32_t>(team_score_offset_)));
        return true;
    }

    bool round_state(RoundState& output, std::string& error) {
        if (!ready(error)) return false;
        CEntityInstance* proxy{};
        void* rules = game_rules(proxy, error);
        if (!rules) return false;
        output = {};
        output.valid = true;
        output.frozen = field<bool>(rules, freeze_period_offset_);
        output.number = field<int>(rules, total_rounds_offset_);
        output.win_status = field<int>(rules, round_win_status_offset_);
        output.win_reason = field<int>(rules, round_win_reason_offset_);
        output.restart_time = field<float>(rules, restart_time_offset_);
        return true;
    }

    bool round_restart(float delay, std::string& error) {
        if (!ready(error)) return false;
        if (delay < 0.0f || delay > 3600.0f) {
            error = "restart delay must be between 0 and 3600 seconds";
            return false;
        }
        ConVarRefAbstract restart("mp_restartgame");
        if (!restart.IsValidRef() || !restart.IsConVarDataValid()) {
            error = "mp_restartgame cvar is unavailable";
            return false;
        }
        char value[64]{};
        std::snprintf(value, sizeof(value), "%.3f", delay);
        if (!restart.SetString(value, CSplitScreenSlot(0))) {
            error = "CS2 rejected mp_restartgame";
            return false;
        }
        return true;
    }

    bool round_terminate(float delay, int reason, std::string& error) {
        if (!ready(error)) return false;
        CEntityInstance* proxy{};
        void* rules = game_rules(proxy, error);
        if (!rules) return false;
        if (delay < 0.0f || reason < 0 || reason > 255) {
            error = "invalid round termination delay or reason";
            return false;
        }
        terminate_round_(rules, delay, reason, nullptr, 0);
        return true;
    }

    bool round_set_frozen(bool frozen, std::string& error) {
        if (!ready(error)) return false;
        CEntityInstance* proxy{};
        void* rules = game_rules(proxy, error);
        if (!rules) return false;
        field<bool>(rules, freeze_period_offset_) = frozen;
        proxy->NetworkStateChanged(NetworkStateChangedData(
            {static_cast<std::uint32_t>(game_rules_pointer_offset_),
             static_cast<std::uint32_t>(freeze_period_offset_)}));
        return true;
    }

    bool entity_create(std::string_view classname, EntityInfo& output,
                       std::string& error) {
        if (!ready(error)) return false;
        if (classname.empty() || classname.size() >= kEntityClassnameCapacity) {
            error = "entity classname is empty or too long";
            return false;
        }
        const std::string owned(classname);
        CEntityInstance* created = create_entity_(owned.c_str(), -1);
        if (!created || !created->m_pEntity) {
            error = "UTIL_CreateEntityByName failed for '" + owned + "'";
            return false;
        }
        return entity_info(created->GetRefEHandle().GetEntryIndex(), output,
                           error);
    }

    bool entity_spawn(int index, std::string& error) {
        if (!ready(error)) return false;
        CEntityInstance* target = entity(index);
        if (!target) {
            error = "entity index is invalid";
            return false;
        }
        dispatch_spawn_(target, nullptr);
        return true;
    }

    bool entity_remove(int index, std::string& error) {
        if (!ready(error)) return false;
        CEntityInstance* target = entity(index);
        if (!target) {
            error = "entity index is invalid";
            return false;
        }
        remove_entity_(target);
        return true;
    }

    bool entity_teleport(int index, const Vector3* position,
                         const Vector3* angles, const Vector3* velocity,
                         std::string& error) {
        if (!ready(error)) return false;
        CEntityInstance* target = entity(index);
        if (!target) {
            error = "entity index is invalid";
            return false;
        }
        const Vector native_position = to_vector(position);
        const QAngle native_angles = to_angle(angles);
        const Vector native_velocity = to_vector(velocity);
        const auto teleport = virtual_function<TeleportFn>(target,
                                                           teleport_index_);
        if (!teleport) {
            error = "CBaseEntity::Teleport is unavailable";
            return false;
        }
        teleport(target, position ? &native_position : nullptr,
                 angles ? &native_angles : nullptr,
                 velocity ? &native_velocity : nullptr);
        return true;
    }

    bool entity_set_owner(int index, int owner_index, std::string& error) {
        if (!ready(error)) return false;
        CEntityInstance* target = entity(index);
        if (!target) {
            error = "entity index is invalid";
            return false;
        }
        CEntityHandle owner_handle;
        if (owner_index >= 0) {
            CEntityInstance* owner = entity(owner_index);
            if (!owner) {
                error = "owner entity index is invalid";
                return false;
            }
            owner_handle = owner->GetRefEHandle();
        }
        field<CEntityHandle>(target, owner_offset_) = owner_handle;
        target->NetworkStateChanged(NetworkStateChangedData(
            static_cast<std::uint32_t>(owner_offset_)));
        return true;
    }

    bool entity_set_parent(int index, int parent_index, std::string& error) {
        if (!ready(error)) return false;
        CEntityInstance* target = entity(index);
        if (!target) {
            error = "entity index is invalid";
            return false;
        }
        if (parent_index < 0) {
            variant_t empty("");
            accept_input_(target, "ClearParent", nullptr, target, &empty, 0,
                          nullptr);
            return true;
        }
        CEntityInstance* parent = entity(parent_index);
        if (!parent) {
            error = "parent entity index is invalid";
            return false;
        }
        variant_t value("!activator");
        accept_input_(target, "SetParent", parent, target, &value, 0, nullptr);
        return true;
    }

    bool entity_accept_input(int index, std::string_view input,
                             std::string_view value, int activator_index,
                             int caller_index, float delay,
                             std::string& error) {
        if (!ready(error)) return false;
        CEntityInstance* target = entity(index);
        if (!target || input.empty()) {
            error = "entity or input name is invalid";
            return false;
        }
        CEntityInstance* activator =
            activator_index >= 0 ? entity(activator_index) : nullptr;
        CEntityInstance* caller =
            caller_index >= 0 ? entity(caller_index) : nullptr;
        if (activator_index >= 0 && !activator) {
            error = "activator entity index is invalid";
            return false;
        }
        if (caller_index >= 0 && !caller) {
            error = "caller entity index is invalid";
            return false;
        }
        const std::string owned_input(input);
        const std::string owned_value(value);
        variant_t variant(owned_value.c_str());
        if (delay > 0.0f) {
            add_input_event_(entity_system(), target, owned_input.c_str(),
                             activator, caller, &variant, delay, nullptr,
                             nullptr);
        } else {
            accept_input_(target, owned_input.c_str(), activator, caller,
                          &variant, 0, nullptr);
        }
        return true;
    }

    bool sound_emit(std::string_view name, const SoundRequest& request,
                    SoundInfo& output, std::string& error) {
        if (!ready(error)) return false;
        if (name.empty() || name.size() >= kSoundNameCapacity) {
            error = "sound name is empty or too long";
            return false;
        }
        if (request.recipients_mask == 0) {
            error = "sound recipient filter is empty";
            return false;
        }
        if (request.volume < 0.0f || request.volume > 10.0f ||
            request.pitch < 1 || request.pitch > 255 ||
            request.delay < 0.0f) {
            error = "invalid volume, pitch, or delay";
            return false;
        }
        int source_index = request.source_entity_index;
        CEntityInstance* source = entity(source_index);
        if (!source) {
            auto worldspawn = find("worldspawn", false, error);
            if (worldspawn.empty()) {
                error = "sound source and worldspawn are unavailable";
                return false;
            }
            source_index = worldspawn.front();
            source = entity(source_index);
        }

        const std::string owned(name);
        EmitSoundParams params;
        params.sound_name = owned.c_str();
        params.volume = request.volume;
        params.sound_time = request.delay;
        params.pitch = static_cast<std::int16_t>(request.pitch);
        if (request.has_origin) {
            params.origin = to_vector(&request.origin);
            params.flags |= (1u << 4);
        }
        LuaCSRecipientFilter filter(request.recipients_mask,
                                    request.reliable);
        const StartSoundEventInfo emitted = emit_sound_filter_(
            filter, CEntityIndex(source_index), params);
        if (emitted.sound.guid == 0) {
            error = "CS2 did not return a sound event GUID";
            return false;
        }

        ActiveSound active;
        active.guid = emitted.sound.guid;
        active.stack_hash = emitted.sound.stack_hash;
        active.source_entity_index = source_index;
        active.recipients_mask = request.recipients_mask;
        active.channel = request.channel;
        active.name = owned;
        active_sounds_[active.guid] = active;

        output = {};
        output.valid = true;
        output.guid = active.guid;
        output.stack_hash = active.stack_hash;
        output.source_entity_index = source_index;
        output.recipients_mask = request.recipients_mask;
        output.channel = request.channel;
        copy_text(output.name, sizeof(output.name), owned);
        return true;
    }

    bool sound_stop(std::uint32_t guid, std::uint64_t recipients,
                    bool reliable, std::string& error) {
        if (!ready(error)) return false;
        if (guid == 0 || recipients == 0) {
            error = "sound GUID and recipient filter must be non-zero";
            return false;
        }
        if (!post_stop_message(guid, recipients, reliable, error)) return false;
        const auto found = active_sounds_.find(guid);
        if (found != active_sounds_.end()) {
            found->second.recipients_mask &= ~recipients;
            if (found->second.recipients_mask == 0) active_sounds_.erase(found);
        }
        return true;
    }

    std::size_t sound_stop_channel(int channel, std::uint64_t recipients,
                                   bool reliable, std::string& error) {
        if (!ready(error)) return 0;
        std::vector<std::pair<std::uint32_t, std::uint64_t>> stops;
        for (const auto& [guid, sound] : active_sounds_) {
            if (sound.channel != channel) continue;
            const std::uint64_t mask = sound.recipients_mask & recipients;
            if (mask) stops.emplace_back(guid, mask);
        }
        for (const auto& [guid, mask] : stops) {
            if (!sound_stop(guid, mask, reliable, error)) return 0;
        }
        return stops.size();
    }

    WorldServices services{};

private:
    template <typename T>
    static T& field(void* base, std::ptrdiff_t offset) {
        return *reinterpret_cast<T*>(
            reinterpret_cast<std::uint8_t*>(base) + offset);
    }

    static Vector3 to_luacs(const Vector& value) {
        return {value.x, value.y, value.z};
    }
    static Vector3 to_luacs(const QAngle& value) {
        return {value.x, value.y, value.z};
    }
    static Vector to_vector(const Vector3* value) {
        return value ? Vector(value->x, value->y, value->z) : Vector();
    }
    static QAngle to_angle(const Vector3* value) {
        return value ? QAngle(value->x, value->y, value->z) : QAngle();
    }

    bool ready(std::string& error) {
        if (!initialize(error)) return false;
        if (!entity_system()) {
            error = "GameEntitySystem is not ready; retry after map startup";
            return false;
        }
        return true;
    }

    bool init_fail(std::string message, std::string& error) {
        initialization_error_ = std::move(message);
        error = initialization_error_;
        return false;
    }

    bool post_stop_message(std::uint32_t guid, std::uint64_t recipients,
                           bool reliable, std::string& error) {
        INetworkMessageInternal* message =
            network_messages_->FindNetworkMessagePartial("SosStopSoundEvent");
        if (!message) {
            message = network_messages_->FindNetworkMessagePartial(
                "CMsgSosStopSoundEvent");
        }
        if (!message) {
            error = "CMsgSosStopSoundEvent network message is unavailable";
            return false;
        }
        auto* data =
            message->AllocateMessage()->ToPB<CMsgSosStopSoundEvent>();
        if (!data) {
            error = "could not allocate CMsgSosStopSoundEvent";
            return false;
        }
        data->set_soundevent_guid(static_cast<std::int32_t>(guid));
        CPlayerBitVec player_bits;
        for (int slot = 0; slot < 64; ++slot) {
            if (recipients & (std::uint64_t{1} << slot)) player_bits.Set(slot);
        }
        game_event_system_->PostEventAbstract(
            CSplitScreenSlot(-1), false, 64,
            reinterpret_cast<const std::uint64_t*>(player_bits.Base()),
            message, data, 0,
            reliable ? NetChannelBufType_t::BUF_RELIABLE
                     : NetChannelBufType_t::BUF_UNRELIABLE);
        delete data;
        return true;
    }

    static LuaCSWorldApi* self(void* context) {
        return static_cast<LuaCSWorldApi*>(context);
    }

#define BOOL_BRIDGE(name, parameters, arguments)                              \
    static bool name##_bridge parameters {                                    \
        auto* api = self(context);                                             \
        if (error && error_size) error[0] = '\0';                              \
        std::string message;                                                   \
        const bool result = api->name arguments;                               \
        if (!result) copy_text(error, error_size, message);                    \
        return result;                                                         \
    }

    BOOL_BRIDGE(team_get_score,
                (void* context, int team, int* output, char* error,
                 std::size_t error_size),
                (team, *output, message))
    BOOL_BRIDGE(team_set_score,
                (void* context, int team, int score, char* error,
                 std::size_t error_size),
                (team, score, message))
    BOOL_BRIDGE(round_state,
                (void* context, RoundState* output, char* error,
                 std::size_t error_size),
                (*output, message))
    BOOL_BRIDGE(round_restart,
                (void* context, float delay, char* error,
                 std::size_t error_size),
                (delay, message))
    BOOL_BRIDGE(round_terminate,
                (void* context, float delay, int reason, char* error,
                 std::size_t error_size),
                (delay, reason, message))
    BOOL_BRIDGE(round_set_frozen,
                (void* context, bool frozen, char* error,
                 std::size_t error_size),
                (frozen, message))
    BOOL_BRIDGE(entity_get,
                (void* context, int index, EntityInfo* output, char* error,
                 std::size_t error_size),
                (index, *output, message))
    BOOL_BRIDGE(entity_create,
                (void* context, const char* classname, EntityInfo* output,
                 char* error, std::size_t error_size),
                (classname ? std::string_view(classname) : std::string_view(),
                 *output, message))
    BOOL_BRIDGE(entity_spawn,
                (void* context, int index, char* error,
                 std::size_t error_size),
                (index, message))
    BOOL_BRIDGE(entity_remove,
                (void* context, int index, char* error,
                 std::size_t error_size),
                (index, message))
    BOOL_BRIDGE(entity_teleport,
                (void* context, int index, const Vector3* position,
                 const Vector3* angles, const Vector3* velocity, char* error,
                 std::size_t error_size),
                (index, position, angles, velocity, message))
    BOOL_BRIDGE(entity_set_owner,
                (void* context, int index, int owner, char* error,
                 std::size_t error_size),
                (index, owner, message))
    BOOL_BRIDGE(entity_set_parent,
                (void* context, int index, int parent, char* error,
                 std::size_t error_size),
                (index, parent, message))
    BOOL_BRIDGE(entity_accept_input,
                (void* context, int index, const char* input,
                 const char* value, int activator, int caller, float delay,
                 char* error, std::size_t error_size),
                (index, input ? std::string_view(input) : std::string_view(),
                 value ? std::string_view(value) : std::string_view(),
                 activator, caller, delay, message))
    BOOL_BRIDGE(sound_emit,
                (void* context, const char* name, const SoundRequest* request,
                 SoundInfo* output, char* error, std::size_t error_size),
                (name ? std::string_view(name) : std::string_view(), *request,
                 *output, message))
    BOOL_BRIDGE(sound_stop,
                (void* context, std::uint32_t guid,
                 std::uint64_t recipients, bool reliable, char* error,
                 std::size_t error_size),
                (guid, recipients, reliable, message))
#undef BOOL_BRIDGE

    static std::size_t entity_count_bridge(
        void* context, const char* pattern, bool by_name, char* error,
        std::size_t error_size) {
        if (error && error_size) error[0] = '\0';
        std::string message;
        auto matches = self(context)->find(
            pattern ? std::string_view(pattern) : std::string_view("*"),
            by_name, message);
        if (!message.empty()) copy_text(error, error_size, message);
        return matches.size();
    }

    static bool entity_at_bridge(void* context, const char* pattern,
                                 bool by_name, std::size_t index,
                                 EntityInfo* output, char* error,
                                 std::size_t error_size) {
        if (error && error_size) error[0] = '\0';
        std::string message;
        auto* api = self(context);
        auto matches = api->find(
            pattern ? std::string_view(pattern) : std::string_view("*"),
            by_name, message);
        if (index >= matches.size()) {
            message = "entity result index is out of range";
            copy_text(error, error_size, message);
            return false;
        }
        const bool result = output &&
                            api->entity_info(matches[index], *output, message);
        if (!result) copy_text(error, error_size, message);
        return result;
    }

    static std::size_t sound_stop_channel_bridge(
        void* context, int channel, std::uint64_t recipients, bool reliable,
        char* error, std::size_t error_size) {
        if (error && error_size) error[0] = '\0';
        std::string message;
        const std::size_t result = self(context)->sound_stop_channel(
            channel, recipients, reliable, message);
        if (!message.empty()) copy_text(error, error_size, message);
        return result;
    }

    bool initialized_{false};
    bool attempted_{false};
    std::string initialization_error_;

    void* game_resource_service_{};
    std::size_t entity_system_offset_{};
    mutable CGameEntitySystem* entity_system_{};
    CSchemaSystem* schema_system_{};
    CSchemaSystemTypeScope* scope_{};
    ICvar* cvars_{};
    IGameEventSystem* game_event_system_{};
    INetworkMessages* network_messages_{};

    std::ptrdiff_t health_offset_{};
    std::ptrdiff_t team_offset_{};
    std::ptrdiff_t velocity_offset_{};
    std::ptrdiff_t body_offset_{};
    std::ptrdiff_t owner_offset_{};
    std::ptrdiff_t scene_node_offset_{};
    std::ptrdiff_t origin_offset_{};
    std::ptrdiff_t rotation_offset_{};
    std::ptrdiff_t parent_node_offset_{};
    std::ptrdiff_t node_owner_offset_{};
    std::ptrdiff_t team_score_offset_{};
    std::ptrdiff_t game_rules_pointer_offset_{};
    std::ptrdiff_t freeze_period_offset_{};
    std::ptrdiff_t total_rounds_offset_{};
    std::ptrdiff_t round_win_status_offset_{};
    std::ptrdiff_t round_win_reason_offset_{};
    std::ptrdiff_t restart_time_offset_{};

    std::size_t teleport_index_{};
    TerminateRoundFn terminate_round_{};
    CreateEntityFn create_entity_{};
    DispatchSpawnFn dispatch_spawn_{};
    RemoveEntityFn remove_entity_{};
    AcceptInputFn accept_input_{};
    AddInputEventFn add_input_event_{};
    EmitSoundFilterFn emit_sound_filter_{};
    std::unordered_map<std::uint32_t, ActiveSound> active_sounds_;
};

LuaCSWorldApi g_world_api;

} // namespace

extern "C" __declspec(dllexport) const luacs::WorldServices*
LuaCS_GetWorldServices() {
    return &g_world_api.services;
}
