#pragma once

#include "game_api.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <entity2/entitysystem.h>
#include <entityhandle.h>
#include <icvar.h>
#include <igameevents.h>
#include <schemasystem/schemasystem.h>
#include <tier1/utlvector.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>

struct LuaCSGameApiImpl {
    using ClientPrintFn = void(__fastcall*)(void*, int, const char*, void*, void*,
                                            void*, void*);
    using ClientPrintAllFn = void(__fastcall*)(int, const char*, void*, void*,
                                               void*, void*, void*);
    using GiveNamedItemFn = void*(__fastcall*)(void*, const char*, void*, void*,
                                               void*, void*);
    using RemoveWeaponsFn = void(__fastcall*)(void*);
    using DropWeaponFn = void(__fastcall*)(void*, void*, const void*, const void*);
    using SelectWeaponFn = void(__fastcall*)(void*, void*, int);
    using RemovePlayerItemFn = void(__fastcall*)(void*, void*);
    using RemoveEntityFn = void(__fastcall*)(void*);
    using SwitchTeamFn = void(__fastcall*)(void*, unsigned char);
    using SetPawnFn = void(__fastcall*)(void*, void*, bool, bool);
    using TeleportFn = void(__fastcall*)(void*, const void*, const void*,
                                         const void*);
    using CommitSuicideFn = void(__fastcall*)(void*, bool, bool);
    using RespawnFn = void(__fastcall*)(void*);
    using ChangeTeamFn = void(__fastcall*)(void*, int);

    struct EventContext {
        IGameEvent* event{};
        bool post{false};
        bool cancelled{false};
        bool dont_broadcast{false};
    };

    void* game_resource_service{};
    std::size_t game_entity_system_offset{};
    mutable CGameEntitySystem* entity_system{};
    CSchemaSystem* schema_system{};
    CSchemaSystemTypeScope* server_scope{};
    ICvar* cvars{};
    IGameEventManager2* event_manager{};
    void* event_manager_vtable{};

    ClientPrintFn client_print{};
    ClientPrintAllFn client_print_all{};
    RemovePlayerItemFn remove_player_item{};
    RemoveEntityFn remove_entity{};
    SwitchTeamFn switch_team{};
    SetPawnFn set_pawn{};

    std::size_t give_named_item_index{};
    std::size_t remove_weapons_index{};
    std::size_t drop_active_weapon_index{};
    std::size_t drop_weapon_index{};
    std::size_t select_weapon_index{};
    std::size_t teleport_index{};
    std::size_t commit_suicide_index{};
    std::size_t respawn_index{};
    std::size_t change_team_index{};

    std::ptrdiff_t controller_pawn_offset{};
    std::ptrdiff_t controller_money_services_offset{};
    std::ptrdiff_t controller_ping_offset{};

    std::ptrdiff_t health_offset{};
    std::ptrdiff_t max_health_offset{};
    std::ptrdiff_t life_state_offset{};
    std::ptrdiff_t team_offset{};
    std::ptrdiff_t flags_offset{};
    std::ptrdiff_t velocity_offset{};
    std::ptrdiff_t body_component_offset{};
    std::ptrdiff_t owner_handle_offset{};

    std::ptrdiff_t item_services_offset{};
    std::ptrdiff_t weapon_services_offset{};
    std::ptrdiff_t eye_angles_offset{};
    std::ptrdiff_t armor_offset{};

    std::ptrdiff_t has_defuser_offset{};
    std::ptrdiff_t has_helmet_offset{};
    std::ptrdiff_t account_offset{};

    std::ptrdiff_t weapons_vector_offset{};
    std::ptrdiff_t active_weapon_offset{};
    std::ptrdiff_t last_weapon_offset{};
    std::ptrdiff_t ammo_offset{};
    std::ptrdiff_t prevent_pickup_offset{};

    std::ptrdiff_t clip1_offset{};
    std::ptrdiff_t clip2_offset{};
    std::ptrdiff_t reserve_ammo_offset{};

    std::ptrdiff_t scene_node_offset{};
    std::ptrdiff_t abs_origin_offset{};

    std::uint64_t next_event_token{1};
    std::unordered_map<std::uint64_t, EventContext> event_contexts;

    CGameEntitySystem* current_entity_system() const;
    CEntityInstance* entity_by_index(int index) const;
    CEntityInstance* entity_by_handle(const CEntityHandle& handle) const;
    CEntityInstance* controller(int slot, std::string& error) const;
    CEntityInstance* pawn(int slot, std::string& error) const;
    int slot_from_pawn(CEntityInstance* player_pawn) const;

    void* service_pointer(CEntityInstance* player_pawn, std::ptrdiff_t offset,
                          const char* name, std::string& error) const;

    template <typename Function>
    static Function virtual_function(void* object, std::size_t index) {
        if (!object) return nullptr;
        auto** table = *reinterpret_cast<void***>(object);
        return table ? reinterpret_cast<Function>(table[index]) : nullptr;
    }

    void state_changed(CEntityInstance* entity, std::uint32_t offset,
                       int array_index = -1) const;
    void state_changed(CEntityInstance* entity, std::uint32_t outer_offset,
                       std::uint32_t inner_offset, int array_index = -1) const;

    bool player_state(int slot, luacs::PlayerState& output,
                      std::string& error) const;
    bool player_set_int(int slot, luacs::PlayerIntField field, int value,
                        std::string& error) const;
    bool player_set_bool(int slot, luacs::PlayerBoolField field, bool value,
                         std::string& error) const;
    bool player_teleport(int slot, const luacs::Vector3* position,
                         const luacs::Vector3* angles,
                         const luacs::Vector3* velocity,
                         std::string& error) const;
    bool player_kill(int slot, bool explode, std::string& error) const;
    bool player_respawn(int slot, std::string& error) const;
    bool player_change_team(int slot, int team, bool use_switch,
                            std::string& error) const;

    bool weapon_give(int slot, std::string_view classname,
                     luacs::WeaponInfo& output, std::string& error) const;
    bool weapon_remove_all(int slot, std::string& error) const;
    bool weapon_drop_active(int slot, std::string& error) const;
    std::size_t weapon_count(int slot, std::string& error) const;
    bool weapon_at(int slot, std::size_t index, luacs::WeaponInfo& output,
                   std::string& error) const;
    bool weapon_get(int entity_index, luacs::WeaponInfo& output,
                    std::string& error) const;
    bool weapon_remove(int slot, int entity_index, bool delete_entity,
                       std::string& error) const;
    bool weapon_drop(int slot, int entity_index, std::string& error) const;
    bool weapon_switch(int slot, int entity_index, std::string& error) const;
    bool weapon_set_clip(int entity_index, int clip_index, int value,
                         std::string& error) const;
    bool weapon_set_reserve(int entity_index, int reserve_index, int value,
                            std::string& error) const;
    bool weapon_get_ammo(int slot, int ammo_type, int& output,
                         std::string& error) const;
    bool weapon_set_ammo(int slot, int ammo_type, int value,
                         std::string& error) const;

    bool fill_weapon(CEntityInstance* weapon, int owner_slot,
                     luacs::WeaponInfo& output, std::string& error) const;
    CUtlVector<CEntityHandle>* inventory(int slot, void*& weapon_services,
                                         std::string& error) const;

    bool hud_print(int slot, int destination, std::string_view message,
                   std::string& error) const;
    bool cvar_exists(std::string_view name) const;
    bool cvar_get(std::string_view name, std::string& value,
                  std::string& error) const;
    bool cvar_set(std::string_view name, std::string_view value,
                  std::string& error) const;

    EventContext* event_context(std::uint64_t token);
    const EventContext* event_context(std::uint64_t token) const;
};

namespace luacs_game_internal {

using FactoryFn = void*(__cdecl*)(const char*, int*);

FactoryFn module_factory(const wchar_t* module_name);
std::string read_file(const std::filesystem::path& path);
void* find_pattern(HMODULE module, std::string_view text);
void* find_virtual_table(HMODULE module, std::string_view class_name);
bool valid_slot(int slot);
bool valid_destination(int destination);

} // namespace luacs_game_internal
