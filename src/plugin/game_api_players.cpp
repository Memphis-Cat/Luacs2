#include "game_api_internal.h"

#include <const.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <limits>

namespace {

template <typename T>
T& field(void* object, std::ptrdiff_t offset) {
    return *reinterpret_cast<T*>(reinterpret_cast<std::uint8_t*>(object) + offset);
}

template <typename T>
const T& field(const void* object, std::ptrdiff_t offset) {
    return *reinterpret_cast<const T*>(
        reinterpret_cast<const std::uint8_t*>(object) + offset);
}

luacs::Vector3 read_vector(const void* object, std::ptrdiff_t offset) {
    const auto* values = reinterpret_cast<const float*>(
        reinterpret_cast<const std::uint8_t*>(object) + offset);
    return {values[0], values[1], values[2]};
}

bool inventory_contains(const CUtlVector<CEntityHandle>* inventory,
                        const CEntityHandle& handle) {
    if (!inventory) return false;
    for (int index = 0; index < inventory->Count(); ++index) {
        if ((*inventory)[index] == handle) return true;
    }
    return false;
}

int inventory_remove_handle(CUtlVector<CEntityHandle>* inventory,
                            const CEntityHandle& handle) {
    if (!inventory) return 0;
    int removed = 0;
    for (int index = inventory->Count() - 1; index >= 0; --index) {
        if ((*inventory)[index] != handle) continue;
        inventory->Remove(index);
        ++removed;
    }
    return removed;
}

} // namespace

bool LuaCSGameApiImpl::player_state(int slot, luacs::PlayerState& output,
                                    std::string& error) const {
    output = {};
    CEntityInstance* player_controller = controller(slot, error);
    if (!player_controller) return false;

    output.valid = true;
    output.has_controller = true;
    output.controller_index = player_controller->GetEntityIndex().Get();
    output.team = static_cast<int>(field<std::uint8_t>(player_controller,
                                                       team_offset));
    output.ping = static_cast<int>(std::min<std::uint32_t>(
        field<std::uint32_t>(player_controller, controller_ping_offset),
        static_cast<std::uint32_t>(std::numeric_limits<int>::max())));

    void* money_services = field<void*>(player_controller,
                                        controller_money_services_offset);
    if (money_services) {
        output.money = field<int>(money_services, account_offset);
    }

    const CEntityHandle pawn_handle = field<CEntityHandle>(
        player_controller, controller_pawn_offset);
    output.pawn_handle = static_cast<std::uint32_t>(pawn_handle.ToInt());
    CEntityInstance* player_pawn = entity_by_handle(pawn_handle);
    if (!player_pawn) return true;

    output.has_pawn = true;
    output.pawn_index = player_pawn->GetEntityIndex().Get();
    output.health = field<int>(player_pawn, health_offset);
    output.max_health = field<int>(player_pawn, max_health_offset);
    output.armor = field<int>(player_pawn, armor_offset);
    const auto life_state = field<std::uint8_t>(player_pawn, life_state_offset);
    output.alive = life_state == 0 && output.health > 0;
    const auto flags = field<std::uint32_t>(player_pawn, flags_offset);
    output.on_ground = (flags & FL_ONGROUND) != 0;
    output.velocity = read_vector(player_pawn, velocity_offset);
    output.eye_angles = read_vector(player_pawn, eye_angles_offset);

    void* body_component = field<void*>(player_pawn, body_component_offset);
    if (body_component) {
        void* scene_node = field<void*>(body_component, scene_node_offset);
        if (scene_node) output.position = read_vector(scene_node, abs_origin_offset);
    }

    void* item_services = field<void*>(player_pawn, item_services_offset);
    if (item_services) {
        output.defuser = field<bool>(item_services, has_defuser_offset);
        output.helmet = field<bool>(item_services, has_helmet_offset);
    }
    return true;
}

bool LuaCSGameApiImpl::player_set_int(int slot,
                                      luacs::PlayerIntField target,
                                      int value, std::string& error) const {
    if (target == luacs::PlayerIntField::Money) {
        CEntityInstance* player_controller = controller(slot, error);
        if (!player_controller) return false;
        void* money_services = field<void*>(player_controller,
                                            controller_money_services_offset);
        if (!money_services) {
            error = "player controller has no in-game money services";
            return false;
        }
        field<int>(money_services, account_offset) = value;
        state_changed(player_controller,
                      static_cast<std::uint32_t>(controller_money_services_offset),
                      static_cast<std::uint32_t>(account_offset));
        return true;
    }

    CEntityInstance* player_pawn = pawn(slot, error);
    if (!player_pawn) return false;
    switch (target) {
        case luacs::PlayerIntField::Health:
            field<int>(player_pawn, health_offset) = value;
            state_changed(player_pawn,
                          static_cast<std::uint32_t>(health_offset));
            return true;
        case luacs::PlayerIntField::Armor:
            field<int>(player_pawn, armor_offset) = value;
            state_changed(player_pawn,
                          static_cast<std::uint32_t>(armor_offset));
            return true;
        default:
            error = "unknown integer player field";
            return false;
    }
}

bool LuaCSGameApiImpl::player_set_bool(int slot,
                                       luacs::PlayerBoolField target,
                                       bool value, std::string& error) const {
    CEntityInstance* player_pawn = pawn(slot, error);
    if (!player_pawn) return false;

    if (target == luacs::PlayerBoolField::PreventWeaponPickup) {
        void* weapon_services = service_pointer(player_pawn,
                                                weapon_services_offset,
                                                "weapon services", error);
        if (!weapon_services) return false;
        field<bool>(weapon_services, prevent_pickup_offset) = value;
        state_changed(player_pawn,
                      static_cast<std::uint32_t>(weapon_services_offset),
                      static_cast<std::uint32_t>(prevent_pickup_offset));
        return true;
    }

    void* item_services = service_pointer(player_pawn, item_services_offset,
                                          "item services", error);
    if (!item_services) return false;
    switch (target) {
        case luacs::PlayerBoolField::Helmet:
            field<bool>(item_services, has_helmet_offset) = value;
            state_changed(player_pawn,
                          static_cast<std::uint32_t>(item_services_offset),
                          static_cast<std::uint32_t>(has_helmet_offset));
            return true;
        case luacs::PlayerBoolField::Defuser:
            field<bool>(item_services, has_defuser_offset) = value;
            state_changed(player_pawn,
                          static_cast<std::uint32_t>(item_services_offset),
                          static_cast<std::uint32_t>(has_defuser_offset));
            return true;
        default:
            error = "unknown boolean player field";
            return false;
    }
}

bool LuaCSGameApiImpl::player_teleport(
    int slot, const luacs::Vector3* position, const luacs::Vector3* angles,
    const luacs::Vector3* velocity, std::string& error) const {
    if (!position && !angles && !velocity) {
        error = "teleport requires a position, angles, or velocity value";
        return false;
    }
    CEntityInstance* player_pawn = pawn(slot, error);
    if (!player_pawn) return false;
    const auto function = virtual_function<TeleportFn>(player_pawn,
                                                       teleport_index);
    if (!function) {
        error = "CBaseEntity::Teleport virtual function is unavailable";
        return false;
    }
    function(player_pawn, position, angles, velocity);
    return true;
}

bool LuaCSGameApiImpl::player_kill(int slot, bool explode,
                                   std::string& error) const {
    CEntityInstance* player_pawn = pawn(slot, error);
    if (!player_pawn) return false;
    const auto function = virtual_function<CommitSuicideFn>(
        player_pawn, commit_suicide_index);
    if (!function) {
        error = "CBasePlayerPawn::CommitSuicide virtual function is unavailable";
        return false;
    }
    function(player_pawn, explode, true);
    return true;
}

bool LuaCSGameApiImpl::player_respawn(int slot, std::string& error) const {
    CEntityInstance* player_controller = controller(slot, error);
    if (!player_controller) return false;
    CEntityInstance* player_pawn = pawn(slot, error);
    if (!player_pawn) return false;
    if (!set_pawn) {
        error = "CBasePlayerController::SetPawn signature is unavailable";
        return false;
    }
    const auto respawn = virtual_function<RespawnFn>(player_controller,
                                                     respawn_index);
    if (!respawn) {
        error = "CCSPlayerController::Respawn virtual function is unavailable";
        return false;
    }
    set_pawn(player_controller, player_pawn, false, false);
    respawn(player_controller);
    return true;
}

bool LuaCSGameApiImpl::player_change_team(int slot, int team,
                                          bool use_switch,
                                          std::string& error) const {
    if (team < 0 || team > 3) {
        error = "team must be 0, 1, 2, or 3";
        return false;
    }
    CEntityInstance* player_controller = controller(slot, error);
    if (!player_controller) return false;
    if (use_switch) {
        if (!switch_team) {
            error = "CCSPlayerController::SwitchTeam signature is unavailable";
            return false;
        }
        switch_team(player_controller, static_cast<unsigned char>(team));
        return true;
    }
    const auto change_team = virtual_function<ChangeTeamFn>(
        player_controller, change_team_index);
    if (!change_team) {
        error = "CCSPlayerController::ChangeTeam virtual function is unavailable";
        return false;
    }
    change_team(player_controller, team);
    return true;
}

CUtlVector<CEntityHandle>* LuaCSGameApiImpl::inventory(
    int slot, void*& weapon_services, std::string& error) const {
    weapon_services = nullptr;
    CEntityInstance* player_pawn = pawn(slot, error);
    if (!player_pawn) return nullptr;
    weapon_services = service_pointer(player_pawn, weapon_services_offset,
                                      "weapon services", error);
    if (!weapon_services) return nullptr;
    return reinterpret_cast<CUtlVector<CEntityHandle>*>(
        reinterpret_cast<std::uint8_t*>(weapon_services) +
        weapons_vector_offset);
}

bool LuaCSGameApiImpl::fill_weapon(CEntityInstance* weapon, int owner_slot,
                                   luacs::WeaponInfo& output,
                                   std::string& error) const {
    output = {};
    if (!weapon || !weapon->m_pEntity) {
        error = "weapon entity is invalid";
        return false;
    }

    output.valid = true;
    output.entity_index = weapon->GetEntityIndex().Get();
    output.handle = static_cast<std::uint32_t>(weapon->GetRefEHandle().ToInt());
    const char* classname = weapon->GetClassname();
    std::snprintf(output.classname, sizeof(output.classname), "%s",
                  classname ? classname : "");
    output.clip1 = field<int>(weapon, clip1_offset);
    output.clip2 = field<int>(weapon, clip2_offset);
    const auto* reserve = reinterpret_cast<const int*>(
        reinterpret_cast<const std::uint8_t*>(weapon) + reserve_ammo_offset);
    output.reserve1 = reserve[0];
    output.reserve2 = reserve[1];

    if (owner_slot < 0) {
        const CEntityHandle owner_handle =
            field<CEntityHandle>(weapon, owner_handle_offset);
        CEntityInstance* owner = entity_by_handle(owner_handle);
        owner_slot = slot_from_pawn(owner);
    }
    output.owner_slot = owner_slot;

    if (owner_slot >= 0) {
        std::string ignored;
        CEntityInstance* owner_pawn = pawn(owner_slot, ignored);
        if (owner_pawn) {
            void* services = field<void*>(owner_pawn, weapon_services_offset);
            if (services) {
                const CEntityHandle active =
                    field<CEntityHandle>(services, active_weapon_offset);
                output.active = active == weapon->GetRefEHandle();
            }
        }
    }
    return true;
}

bool LuaCSGameApiImpl::weapon_give(int slot, std::string_view classname,
                                   luacs::WeaponInfo& output,
                                   std::string& error) const {
    output = {};
    if (classname.empty()) {
        error = "item classname cannot be empty";
        return false;
    }
    CEntityInstance* player_pawn = pawn(slot, error);
    if (!player_pawn) return false;
    void* item_services = service_pointer(player_pawn, item_services_offset,
                                          "item services", error);
    if (!item_services) return false;
    const auto give = virtual_function<GiveNamedItemFn>(item_services,
                                                        give_named_item_index);
    if (!give) {
        error = "CCSPlayer_ItemServices::GiveNamedItem is unavailable";
        return false;
    }
    const std::string owned(classname);
    auto* created = static_cast<CEntityInstance*>(
        give(item_services, owned.c_str(), nullptr, nullptr, nullptr, nullptr));
    if (!created) {
        error = "CS2 could not create item '" + owned + "'";
        return false;
    }
    return fill_weapon(created, slot, output, error);
}

bool LuaCSGameApiImpl::weapon_remove_all(int slot,
                                         std::string& error) const {
    CEntityInstance* player_pawn = pawn(slot, error);
    if (!player_pawn) return false;
    void* item_services = service_pointer(player_pawn, item_services_offset,
                                          "item services", error);
    if (!item_services) return false;
    const auto remove_all = virtual_function<RemoveWeaponsFn>(
        item_services, remove_weapons_index);
    if (!remove_all) {
        error = "CCSPlayer_ItemServices::RemoveWeapons is unavailable";
        return false;
    }
    remove_all(item_services);
    return true;
}

bool LuaCSGameApiImpl::weapon_drop_active(int slot,
                                          std::string& error) const {
    void* weapon_services{};
    auto* weapons = inventory(slot, weapon_services, error);
    if (!weapons || !weapon_services) return false;
    const CEntityHandle active =
        field<CEntityHandle>(weapon_services, active_weapon_offset);
    CEntityInstance* weapon = entity_by_handle(active);
    if (!weapon) {
        error = "player has no active weapon";
        return false;
    }
    const auto drop = virtual_function<DropWeaponFn>(weapon_services,
                                                     drop_weapon_index);
    if (!drop) {
        error = "CCSPlayer_WeaponServices::DropWeapon is unavailable";
        return false;
    }
    drop(weapon_services, weapon, nullptr, nullptr);
    return true;
}

std::size_t LuaCSGameApiImpl::weapon_count(int slot,
                                           std::string& error) const {
    void* weapon_services{};
    auto* weapons = inventory(slot, weapon_services, error);
    if (!weapons || !weapon_services) return 0;

    CEntityInstance* player_pawn = pawn(slot, error);
    if (!player_pawn) return 0;

    bool repaired = false;
    for (int index = weapons->Count() - 1; index >= 0; --index) {
        if (entity_by_handle((*weapons)[index])) continue;
        weapons->Remove(index);
        repaired = true;
    }
    if (repaired) {
        state_changed(player_pawn,
                      static_cast<std::uint32_t>(weapon_services_offset),
                      static_cast<std::uint32_t>(weapons_vector_offset));
    }
    return static_cast<std::size_t>(weapons->Count());
}

bool LuaCSGameApiImpl::weapon_at(int slot, std::size_t index,
                                 luacs::WeaponInfo& output,
                                 std::string& error) const {
    void* weapon_services{};
    auto* weapons = inventory(slot, weapon_services, error);
    if (!weapons || !weapon_services) return false;

    CEntityInstance* player_pawn = pawn(slot, error);
    if (!player_pawn) return false;

    while (index < static_cast<std::size_t>(weapons->Count())) {
        const CEntityHandle handle = (*weapons)[static_cast<int>(index)];
        CEntityInstance* weapon = entity_by_handle(handle);
        if (weapon) return fill_weapon(weapon, slot, output, error);

        weapons->Remove(static_cast<int>(index));
        state_changed(player_pawn,
                      static_cast<std::uint32_t>(weapon_services_offset),
                      static_cast<std::uint32_t>(weapons_vector_offset));
    }

    error = "inventory index is out of range";
    return false;
}

bool LuaCSGameApiImpl::weapon_get(int entity_index,
                                  luacs::WeaponInfo& output,
                                  std::string& error) const {
    CEntityInstance* weapon = entity_by_index(entity_index);
    if (!weapon) {
        error = "weapon entity index is invalid";
        return false;
    }
    return fill_weapon(weapon, -1, output, error);
}

bool LuaCSGameApiImpl::weapon_remove(int slot, int entity_index,
                                     bool delete_entity,
                                     std::string& error) const {
    void* weapon_services{};
    auto* weapons = inventory(slot, weapon_services, error);
    if (!weapons || !weapon_services) return false;

    CEntityInstance* weapon = entity_by_index(entity_index);
    if (!weapon) {
        error = "the selected weapon entity is invalid";
        return false;
    }

    const CEntityHandle weapon_handle = weapon->GetRefEHandle();
    if (!inventory_contains(weapons, weapon_handle)) {
        error = "the selected weapon is not in this player's inventory";
        return false;
    }
    if (delete_entity && !remove_entity) {
        error = "UTIL_Remove signature is unavailable";
        return false;
    }

    CEntityInstance* player_pawn = pawn(slot, error);
    if (!player_pawn) return false;
    if (!remove_player_item) {
        error = "CBasePlayerPawn::RemovePlayerItem signature is unavailable";
        return false;
    }

    remove_player_item(player_pawn, weapon);

    if (inventory_contains(weapons, weapon_handle)) {
        const int removed = inventory_remove_handle(weapons, weapon_handle);
        if (removed <= 0) {
            error = "RemovePlayerItem left the weapon in m_hMyWeapons and LuaCS "
                    "could not remove the stale inventory handle";
            return false;
        }
        state_changed(player_pawn,
                      static_cast<std::uint32_t>(weapon_services_offset),
                      static_cast<std::uint32_t>(weapons_vector_offset));
    }

    auto& active = field<CEntityHandle>(weapon_services, active_weapon_offset);
    if (active == weapon_handle) {
        active.Term();
        state_changed(player_pawn,
                      static_cast<std::uint32_t>(weapon_services_offset),
                      static_cast<std::uint32_t>(active_weapon_offset));
    }

    auto& last = field<CEntityHandle>(weapon_services, last_weapon_offset);
    if (last == weapon_handle) {
        last.Term();
        state_changed(player_pawn,
                      static_cast<std::uint32_t>(weapon_services_offset),
                      static_cast<std::uint32_t>(last_weapon_offset));
    }

    if (inventory_contains(weapons, weapon_handle)) {
        error = "weapon handle is still present in m_hMyWeapons after explicit "
                "inventory repair; refusing to destroy the entity";
        return false;
    }

    if (delete_entity) remove_entity(weapon);
    return true;
}

bool LuaCSGameApiImpl::weapon_drop(int slot, int entity_index,
                                   std::string& error) const {
    void* weapon_services{};
    auto* weapons = inventory(slot, weapon_services, error);
    if (!weapons || !weapon_services) return false;
    CEntityInstance* weapon = entity_by_index(entity_index);
    if (!weapon || !inventory_contains(weapons, weapon->GetRefEHandle())) {
        error = "the selected weapon is not in this player's inventory";
        return false;
    }
    const auto drop = virtual_function<DropWeaponFn>(weapon_services,
                                                     drop_weapon_index);
    if (!drop) {
        error = "CCSPlayer_WeaponServices::DropWeapon is unavailable";
        return false;
    }
    drop(weapon_services, weapon, nullptr, nullptr);
    return true;
}

bool LuaCSGameApiImpl::weapon_switch(int slot, int entity_index,
                                     std::string& error) const {
    void* weapon_services{};
    auto* weapons = inventory(slot, weapon_services, error);
    if (!weapons || !weapon_services) return false;
    CEntityInstance* weapon = entity_by_index(entity_index);
    if (!weapon || !inventory_contains(weapons, weapon->GetRefEHandle())) {
        error = "the selected weapon is not in this player's inventory";
        return false;
    }
    const auto select = virtual_function<SelectWeaponFn>(weapon_services,
                                                         select_weapon_index);
    if (!select) {
        error = "CCSPlayer_WeaponServices::SelectItem is unavailable";
        return false;
    }
    select(weapon_services, weapon, 0);
    return true;
}

bool LuaCSGameApiImpl::weapon_set_clip(int entity_index, int clip_index,
                                       int value, std::string& error) const {
    if (clip_index != 0 && clip_index != 1) {
        error = "clip selector must be 0 or 1";
        return false;
    }
    CEntityInstance* weapon = entity_by_index(entity_index);
    if (!weapon) {
        error = "weapon entity index is invalid";
        return false;
    }
    const auto offset = clip_index == 0 ? clip1_offset : clip2_offset;
    field<int>(weapon, offset) = value;
    state_changed(weapon, static_cast<std::uint32_t>(offset));
    return true;
}

bool LuaCSGameApiImpl::weapon_set_reserve(int entity_index,
                                          int reserve_index, int value,
                                          std::string& error) const {
    if (reserve_index != 0 && reserve_index != 1) {
        error = "reserve selector must be 0 or 1";
        return false;
    }
    CEntityInstance* weapon = entity_by_index(entity_index);
    if (!weapon) {
        error = "weapon entity index is invalid";
        return false;
    }
    auto* reserve = reinterpret_cast<int*>(
        reinterpret_cast<std::uint8_t*>(weapon) + reserve_ammo_offset);
    reserve[reserve_index] = value;
    state_changed(weapon, static_cast<std::uint32_t>(reserve_ammo_offset),
                  reserve_index);
    return true;
}

bool LuaCSGameApiImpl::weapon_get_ammo(int slot, int ammo_type, int& output,
                                       std::string& error) const {
    if (ammo_type < 0 || ammo_type >= 32) {
        error = "ammo type must be between 0 and 31";
        return false;
    }
    CEntityInstance* player_pawn = pawn(slot, error);
    if (!player_pawn) return false;
    void* weapon_services = service_pointer(player_pawn, weapon_services_offset,
                                            "weapon services", error);
    if (!weapon_services) return false;
    const auto* ammo = reinterpret_cast<const std::uint16_t*>(
        reinterpret_cast<const std::uint8_t*>(weapon_services) + ammo_offset);
    output = ammo[ammo_type];
    return true;
}

bool LuaCSGameApiImpl::weapon_set_ammo(int slot, int ammo_type, int value,
                                       std::string& error) const {
    if (ammo_type < 0 || ammo_type >= 32) {
        error = "ammo type must be between 0 and 31";
        return false;
    }
    if (value < 0 || value > std::numeric_limits<std::uint16_t>::max()) {
        error = "ammo value must be between 0 and 65535";
        return false;
    }
    CEntityInstance* player_pawn = pawn(slot, error);
    if (!player_pawn) return false;
    void* weapon_services = service_pointer(player_pawn, weapon_services_offset,
                                            "weapon services", error);
    if (!weapon_services) return false;
    auto* ammo = reinterpret_cast<std::uint16_t*>(
        reinterpret_cast<std::uint8_t*>(weapon_services) + ammo_offset);
    ammo[ammo_type] = static_cast<std::uint16_t>(value);
    state_changed(player_pawn,
                  static_cast<std::uint32_t>(weapon_services_offset),
                  static_cast<std::uint32_t>(ammo_offset), ammo_type);
    return true;
}
