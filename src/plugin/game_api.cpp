#include "game_api_internal.h"

#include <iserver.h>
#include <tier1/bufferstring.h>
#include <tier1/convar.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <limits>
#include <optional>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

namespace {

std::optional<std::string> entry_windows_value(const std::string& json,
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

std::optional<std::size_t> entry_windows_number(const std::string& json,
                                                std::string_view entry_name) {
    const auto value = entry_windows_value(json, entry_name);
    if (!value) return std::nullopt;
    try {
        return static_cast<std::size_t>(std::stoull(*value));
    } catch (...) {
        return std::nullopt;
    }
}

std::vector<int> parse_pattern(std::string_view text) {
    std::vector<int> pattern;
    std::istringstream input{std::string(text)};
    std::string token;
    while (input >> token) {
        if (token == "?" || token == "??") {
            pattern.push_back(-1);
            continue;
        }
        if (token.size() != 2 ||
            !std::isxdigit(static_cast<unsigned char>(token[0])) ||
            !std::isxdigit(static_cast<unsigned char>(token[1]))) {
            return {};
        }
        pattern.push_back(static_cast<int>(std::strtoul(token.c_str(), nullptr, 16)));
    }
    return pattern;
}

struct ImageSection {
    std::uint8_t* data{};
    std::size_t size{};
};

std::optional<ImageSection> image_section(HMODULE module, const char* name) {
    if (!module || !name) return std::nullopt;
    auto* base = reinterpret_cast<std::uint8_t*>(module);
    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return std::nullopt;
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return std::nullopt;

    auto* section = IMAGE_FIRST_SECTION(nt);
    for (unsigned index = 0; index < nt->FileHeader.NumberOfSections;
         ++index, ++section) {
        char section_name[IMAGE_SIZEOF_SHORT_NAME + 1]{};
        std::memcpy(section_name, section->Name, IMAGE_SIZEOF_SHORT_NAME);
        if (std::strcmp(section_name, name) != 0) continue;
        const auto size = static_cast<std::size_t>(
            std::min(section->SizeOfRawData, section->Misc.VirtualSize));
        return ImageSection{base + section->VirtualAddress, size};
    }
    return std::nullopt;
}

std::uint8_t* find_bytes(const ImageSection& section, const void* bytes,
                         std::size_t length, std::size_t start = 0) {
    if (!section.data || !bytes || length == 0 || start > section.size ||
        length > section.size - start) {
        return nullptr;
    }
    const auto* needle = static_cast<const std::uint8_t*>(bytes);
    for (std::size_t offset = start; offset <= section.size - length; ++offset) {
        if (std::memcmp(section.data + offset, needle, length) == 0) {
            return section.data + offset;
        }
    }
    return nullptr;
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

std::optional<std::ptrdiff_t> resolve_schema_field(CSchemaSystemTypeScope* scope,
                                                   const char* class_name,
                                                   const char* field_name) {
    if (!scope) return std::nullopt;
    const auto class_handle = scope->FindDeclaredClass(class_name);
    return find_schema_field(class_handle.Get(), field_name);
}

bool resolve_required_field(CSchemaSystemTypeScope* scope,
                            std::ptrdiff_t& destination,
                            const char* class_name, const char* field_name,
                            std::string& error) {
    const auto value = resolve_schema_field(scope, class_name, field_name);
    if (!value) {
        error = std::string("Source 2 schema field was not found: ") + class_name +
                "::" + field_name;
        return false;
    }
    destination = *value;
    return true;
}

std::string combined_gamedata(const std::filesystem::path& root) {
    const auto reference = root / "gamedata" / "reference";
    std::string result = luacs_game_internal::read_file(
        reference / "official_windows_gamedata.json");
    const auto combined = luacs_game_internal::read_file(
        reference / "combined_namespaced_windows_reference.json");
    if (!combined.empty()) {
        result.push_back('\n');
        result += combined;
    }
    const auto inventory = luacs_game_internal::read_file(
        reference / "inventory_simulator_windows.json");
    if (!inventory.empty()) {
        result.push_back('\n');
        result += inventory;
    }
    return result;
}

} // namespace

namespace luacs_game_internal {

FactoryFn module_factory(const wchar_t* module_name) {
    const HMODULE module = GetModuleHandleW(module_name);
    if (!module) return nullptr;
    return reinterpret_cast<FactoryFn>(GetProcAddress(module, "CreateInterface"));
}

std::string read_file(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) return {};
    std::ostringstream output;
    output << input.rdbuf();
    return output.str();
}

void* find_pattern(HMODULE module, std::string_view text) {
    if (!module) return nullptr;
    const auto pattern = parse_pattern(text);
    if (pattern.empty()) return nullptr;

    const auto* base = reinterpret_cast<const std::uint8_t*>(module);
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return nullptr;
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return nullptr;

    const std::size_t image_size = nt->OptionalHeader.SizeOfImage;
    if (image_size < pattern.size()) return nullptr;
    for (std::size_t offset = 0; offset <= image_size - pattern.size(); ++offset) {
        bool matches = true;
        for (std::size_t index = 0; index < pattern.size(); ++index) {
            if (pattern[index] >= 0 &&
                base[offset + index] != static_cast<std::uint8_t>(pattern[index])) {
                matches = false;
                break;
            }
        }
        if (matches) return const_cast<std::uint8_t*>(base + offset);
    }
    return nullptr;
}

void* find_virtual_table(HMODULE module, std::string_view class_name) {
    const auto data = image_section(module, ".data");
    const auto read_only = image_section(module, ".rdata");
    if (!data || !read_only) return nullptr;

    const std::string decorated = ".?AV" + std::string(class_name) + "@@";
    auto* decorated_location =
        find_bytes(*data, decorated.c_str(), decorated.size() + 1);
    if (!decorated_location || decorated_location < data->data + 0x10) {
        return nullptr;
    }

    auto* type_descriptor = decorated_location - 0x10;
    const auto module_base = reinterpret_cast<std::uintptr_t>(module);
    const auto descriptor_address = reinterpret_cast<std::uintptr_t>(type_descriptor);
    if (descriptor_address < module_base ||
        descriptor_address - module_base > std::numeric_limits<std::uint32_t>::max()) {
        return nullptr;
    }
    const auto descriptor_rva =
        static_cast<std::uint32_t>(descriptor_address - module_base);

    std::size_t search_offset = 0;
    while (auto* descriptor_reference =
               find_bytes(*read_only, &descriptor_rva, sizeof(descriptor_rva),
                          search_offset)) {
        search_offset = static_cast<std::size_t>(
                            descriptor_reference - read_only->data) +
                        sizeof(descriptor_rva);
        if (descriptor_reference < read_only->data + 0x0C) continue;
        auto* locator = descriptor_reference - 0x0C;
        if (*reinterpret_cast<std::int32_t*>(locator) != 1) continue;
        if (*reinterpret_cast<std::int32_t*>(descriptor_reference - 0x08) != 0) {
            continue;
        }

        const auto locator_pointer = reinterpret_cast<std::uintptr_t>(locator);
        auto* vtable_reference = find_bytes(
            *read_only, &locator_pointer, sizeof(locator_pointer));
        if (vtable_reference) return vtable_reference + sizeof(void*);
    }
    return nullptr;
}

bool valid_slot(int slot) { return slot >= 0 && slot < 64; }

bool valid_destination(int destination) {
    return destination == static_cast<int>(luacs::HudDestination::Notify) ||
           destination == static_cast<int>(luacs::HudDestination::Console) ||
           destination == static_cast<int>(luacs::HudDestination::Chat) ||
           destination == static_cast<int>(luacs::HudDestination::Center) ||
           destination == static_cast<int>(luacs::HudDestination::Alert);
}

} // namespace luacs_game_internal

CGameEntitySystem* LuaCSGameApiImpl::current_entity_system() const {
    if (entity_system) return entity_system;
    if (!game_resource_service || game_entity_system_offset == 0) return nullptr;
    auto** location = reinterpret_cast<CGameEntitySystem**>(
        reinterpret_cast<std::uint8_t*>(game_resource_service) +
        game_entity_system_offset);
    if (location) entity_system = *location;
    return entity_system;
}

CEntityInstance* LuaCSGameApiImpl::entity_by_index(int index) const {
    CGameEntitySystem* system = current_entity_system();
    if (!system || index < 0 || index >= MAX_TOTAL_ENTITIES) return nullptr;
    const int chunk_index = index / MAX_ENTITIES_IN_LIST;
    const int entry_index = index % MAX_ENTITIES_IN_LIST;
    CEntityIdentity* chunk = system->m_EntityList.m_pIdentityChunks[chunk_index];
    if (!chunk) return nullptr;
    CEntityIdentity* identity = &chunk[entry_index];
    if (!identity->m_pInstance || identity->m_EHandle.GetEntryIndex() != index) {
        return nullptr;
    }
    return identity->m_pInstance;
}

CEntityInstance* LuaCSGameApiImpl::entity_by_handle(
    const CEntityHandle& handle) const {
    if (!handle.IsValid()) return nullptr;
    CEntityInstance* instance = entity_by_index(handle.GetEntryIndex());
    if (!instance || !instance->m_pEntity ||
        instance->m_pEntity->GetRefEHandle() != handle) {
        return nullptr;
    }
    return instance;
}

CEntityInstance* LuaCSGameApiImpl::controller(int slot,
                                              std::string& error) const {
    if (!luacs_game_internal::valid_slot(slot)) {
        error = "player slot must be between 0 and 63";
        return nullptr;
    }
    if (!current_entity_system()) {
        error = "GameEntitySystem is not ready; retry after a map has started";
        return nullptr;
    }
    CEntityInstance* result = entity_by_index(slot + 1);
    if (!result) {
        error = "no live player controller exists for slot " +
                std::to_string(slot);
    }
    return result;
}

CEntityInstance* LuaCSGameApiImpl::pawn(int slot, std::string& error) const {
    CEntityInstance* player_controller = controller(slot, error);
    if (!player_controller) return nullptr;
    const auto* handle = reinterpret_cast<const CEntityHandle*>(
        reinterpret_cast<const std::uint8_t*>(player_controller) +
        controller_pawn_offset);
    CEntityInstance* result = entity_by_handle(*handle);
    if (!result) {
        error = "player slot " + std::to_string(slot) + " has no live pawn";
    }
    return result;
}

int LuaCSGameApiImpl::slot_from_pawn(CEntityInstance* player_pawn) const {
    if (!player_pawn) return -1;
    const CEntityHandle pawn_handle = player_pawn->GetRefEHandle();
    for (int slot = 0; slot < 64; ++slot) {
        CEntityInstance* player_controller = entity_by_index(slot + 1);
        if (!player_controller) continue;
        const auto* candidate = reinterpret_cast<const CEntityHandle*>(
            reinterpret_cast<const std::uint8_t*>(player_controller) +
            controller_pawn_offset);
        if (*candidate == pawn_handle) return slot;
    }
    return -1;
}

void* LuaCSGameApiImpl::service_pointer(CEntityInstance* player_pawn,
                                        std::ptrdiff_t offset,
                                        const char* name,
                                        std::string& error) const {
    if (!player_pawn) {
        error = "player pawn is unavailable";
        return nullptr;
    }
    void* result = *reinterpret_cast<void**>(
        reinterpret_cast<std::uint8_t*>(player_pawn) + offset);
    if (!result) error = std::string("player pawn has no ") + name;
    return result;
}

void LuaCSGameApiImpl::state_changed(CEntityInstance* entity,
                                     std::uint32_t offset,
                                     int array_index) const {
    if (!entity) return;
    entity->NetworkStateChanged(NetworkStateChangedData(offset, array_index));
}

void LuaCSGameApiImpl::state_changed(CEntityInstance* entity,
                                     std::uint32_t outer_offset,
                                     std::uint32_t inner_offset,
                                     int array_index) const {
    if (!entity) return;
    entity->NetworkStateChanged(
        NetworkStateChangedData({outer_offset, inner_offset}, array_index));
}

bool LuaCSGameApiImpl::hud_print(int slot, int destination,
                                 std::string_view message,
                                 std::string& error) const {
    if (!luacs_game_internal::valid_destination(destination)) {
        error = "invalid HUD destination; expected 1, 2, 3, 4, or 6";
        return false;
    }
    if (message.empty()) {
        error = "HUD message cannot be empty";
        return false;
    }
    const std::string owned(message);
    if (slot == -1) {
        if (!client_print_all) {
            error = "UTIL_ClientPrintAll signature is unavailable";
            return false;
        }
        client_print_all(destination, owned.c_str(), nullptr, nullptr, nullptr,
                         nullptr, nullptr);
        return true;
    }
    CEntityInstance* player_controller = controller(slot, error);
    if (!player_controller) return false;
    if (!client_print) {
        error = "ClientPrint signature is unavailable";
        return false;
    }
    client_print(player_controller, destination, owned.c_str(), nullptr, nullptr,
                 nullptr, nullptr);
    return true;
}

bool LuaCSGameApiImpl::cvar_exists(std::string_view name) const {
    if (!cvars || name.empty()) return false;
    const std::string owned(name);
    ConVarRefAbstract reference(owned.c_str());
    return reference.IsValidRef() && reference.IsConVarDataValid();
}

bool LuaCSGameApiImpl::cvar_get(std::string_view name, std::string& value,
                                std::string& error) const {
    if (!cvars) {
        error = "ICvar is unavailable";
        return false;
    }
    if (name.empty()) {
        error = "cvar name cannot be empty";
        return false;
    }
    const std::string owned(name);
    ConVarRefAbstract reference(owned.c_str());
    if (!reference.IsValidRef() || !reference.IsConVarDataValid()) {
        error = "cvar '" + owned + "' does not exist";
        return false;
    }
    CBufferString buffer;
    reference.GetValueAsString(buffer, CSplitScreenSlot(0));
    value = buffer.Get();
    return true;
}

bool LuaCSGameApiImpl::cvar_set(std::string_view name,
                                std::string_view value,
                                std::string& error) const {
    if (!cvars) {
        error = "ICvar is unavailable";
        return false;
    }
    if (name.empty()) {
        error = "cvar name cannot be empty";
        return false;
    }
    const std::string owned_name(name);
    const std::string owned_value(value);
    ConVarRefAbstract reference(owned_name.c_str());
    if (!reference.IsValidRef() || !reference.IsConVarDataValid()) {
        error = "cvar '" + owned_name + "' does not exist";
        return false;
    }
    if (!reference.SetString(owned_value.c_str(), CSplitScreenSlot(0))) {
        error = "CS2 rejected the new value for cvar '" + owned_name + "'";
        return false;
    }
    return true;
}

LuaCSGameApiImpl::EventContext* LuaCSGameApiImpl::event_context(
    std::uint64_t token) {
    const auto found = event_contexts.find(token);
    return found == event_contexts.end() ? nullptr : &found->second;
}

const LuaCSGameApiImpl::EventContext* LuaCSGameApiImpl::event_context(
    std::uint64_t token) const {
    const auto found = event_contexts.find(token);
    return found == event_contexts.end() ? nullptr : &found->second;
}

LuaCSGameApi::LuaCSGameApi() : impl_(std::make_unique<LuaCSGameApiImpl>()) {}
LuaCSGameApi::~LuaCSGameApi() = default;

bool LuaCSGameApi::initialize(const std::filesystem::path& luacs_root,
                              std::string& error) {
    shutdown();
    error.clear();

    const std::string gamedata = combined_gamedata(luacs_root);
    if (gamedata.empty()) {
        error = "could not read LuaCS Windows gamedata reference pack";
        return false;
    }

    const auto game_entity_system_offset =
        entry_windows_number(gamedata, "GameEntitySystem");
    const auto give_index = entry_windows_number(
        gamedata, "CCSPlayer_ItemServices_GiveNamedItem");
    const auto remove_all_index = entry_windows_number(
        gamedata, "CCSPlayer_ItemServices_RemoveWeapons");
    const auto drop_active_index = entry_windows_number(
        gamedata, "CCSPlayer_ItemServices_DropActivePlayerWeapon");
    const auto drop_weapon_index = entry_windows_number(
        gamedata, "CCSPlayer_WeaponServices::DropWeapon");
    auto select_weapon_index = entry_windows_number(
        gamedata, "CCSPlayer_WeaponServices::SelectItem");

    const auto teleport_index =
        entry_windows_number(gamedata, "CBaseEntity_Teleport");
    const auto suicide_index =
        entry_windows_number(gamedata, "CBasePlayerPawn_CommitSuicide");
    const auto respawn_index =
        entry_windows_number(gamedata, "CCSPlayerController_Respawn");
    const auto change_team_index =
        entry_windows_number(gamedata, "CCSPlayerController_ChangeTeam");

    const auto client_print_pattern =
        entry_windows_value(gamedata, "ClientPrint");
    const auto client_print_all_pattern =
        entry_windows_value(gamedata, "UTIL_ClientPrintAll");
    const auto remove_player_item_pattern =
        entry_windows_value(gamedata, "CBasePlayerPawn_RemovePlayerItem");
    const auto remove_entity_pattern =
        entry_windows_value(gamedata, "UTIL_Remove");
    const auto switch_team_pattern =
        entry_windows_value(gamedata, "CCSPlayerController_SwitchTeam");
    const auto set_pawn_pattern =
        entry_windows_value(gamedata, "CBasePlayerController_SetPawn");

    // This current CS2 virtual index is also stored in the bundled reference
    // pack. Keep the fallback so older LuaCS packages remain upgradeable.
    if (!select_weapon_index) select_weapon_index = 30;

    if (!game_entity_system_offset || !give_index || !remove_all_index ||
        !drop_active_index || !drop_weapon_index || !select_weapon_index ||
        !teleport_index || !suicide_index || !respawn_index ||
        !change_team_index || !client_print_pattern ||
        !client_print_all_pattern || !remove_player_item_pattern ||
        !remove_entity_pattern || !switch_team_pattern || !set_pawn_pattern) {
        error = "Windows gamedata is missing a required player, event, HUD, or inventory entry";
        return false;
    }

    const auto engine_factory =
        luacs_game_internal::module_factory(L"engine2.dll");
    const auto schema_factory =
        luacs_game_internal::module_factory(L"schemasystem.dll");
    const auto cvar_factory =
        luacs_game_internal::module_factory(L"tier0.dll");
    if (!engine_factory || !schema_factory || !cvar_factory) {
        error = "could not resolve engine2, schemasystem, or tier0 CreateInterface";
        return false;
    }

    impl_->game_resource_service =
        engine_factory("GameResourceServiceServerV001", nullptr);
    impl_->game_entity_system_offset = *game_entity_system_offset;
    impl_->schema_system = static_cast<CSchemaSystem*>(
        schema_factory(SCHEMASYSTEM_INTERFACE_VERSION, nullptr));
    impl_->cvars = static_cast<ICvar*>(
        cvar_factory("VEngineCvar007", nullptr));
    if (!impl_->game_resource_service || !impl_->schema_system ||
        !impl_->cvars) {
        error = "could not acquire GameResourceServiceServerV001, SchemaSystem_001, or VEngineCvar007";
        return false;
    }
    g_pCVar = impl_->cvars;

    impl_->server_scope =
        impl_->schema_system->FindTypeScopeForModule("server.dll");
    if (!impl_->server_scope) {
        error = "Source 2 schema scope for server.dll was unavailable";
        return false;
    }

#define RESOLVE_FIELD(member, class_name, field_name)                         \
    if (!resolve_required_field(impl_->server_scope, impl_->member, class_name, \
                                field_name, error)) {                          \
        return false;                                                         \
    }

    RESOLVE_FIELD(controller_pawn_offset, "CCSPlayerController", "m_hPlayerPawn");
    RESOLVE_FIELD(controller_money_services_offset, "CCSPlayerController",
                  "m_pInGameMoneyServices");
    RESOLVE_FIELD(controller_ping_offset, "CCSPlayerController", "m_iPing");
    RESOLVE_FIELD(health_offset, "CBaseEntity", "m_iHealth");
    RESOLVE_FIELD(max_health_offset, "CBaseEntity", "m_iMaxHealth");
    RESOLVE_FIELD(life_state_offset, "CBaseEntity", "m_lifeState");
    RESOLVE_FIELD(team_offset, "CBaseEntity", "m_iTeamNum");
    RESOLVE_FIELD(flags_offset, "CBaseEntity", "m_fFlags");
    RESOLVE_FIELD(velocity_offset, "CBaseEntity", "m_vecAbsVelocity");
    RESOLVE_FIELD(body_component_offset, "CBaseEntity", "m_CBodyComponent");
    RESOLVE_FIELD(owner_handle_offset, "CBaseEntity", "m_hOwnerEntity");
    RESOLVE_FIELD(item_services_offset, "CBasePlayerPawn", "m_pItemServices");
    RESOLVE_FIELD(weapon_services_offset, "CBasePlayerPawn", "m_pWeaponServices");
    RESOLVE_FIELD(eye_angles_offset, "CCSPlayerPawn", "m_angEyeAngles");
    RESOLVE_FIELD(armor_offset, "CCSPlayerPawn", "m_ArmorValue");
    RESOLVE_FIELD(has_defuser_offset, "CCSPlayer_ItemServices", "m_bHasDefuser");
    RESOLVE_FIELD(has_helmet_offset, "CCSPlayer_ItemServices", "m_bHasHelmet");
    RESOLVE_FIELD(account_offset, "CCSPlayerController_InGameMoneyServices",
                  "m_iAccount");
    RESOLVE_FIELD(weapons_vector_offset, "CPlayer_WeaponServices", "m_hMyWeapons");
    RESOLVE_FIELD(active_weapon_offset, "CPlayer_WeaponServices", "m_hActiveWeapon");
    RESOLVE_FIELD(last_weapon_offset, "CPlayer_WeaponServices", "m_hLastWeapon");
    RESOLVE_FIELD(ammo_offset, "CPlayer_WeaponServices", "m_iAmmo");
    RESOLVE_FIELD(prevent_pickup_offset, "CPlayer_WeaponServices",
                  "m_bPreventWeaponPickup");
    RESOLVE_FIELD(clip1_offset, "CBasePlayerWeapon", "m_iClip1");
    RESOLVE_FIELD(clip2_offset, "CBasePlayerWeapon", "m_iClip2");
    RESOLVE_FIELD(reserve_ammo_offset, "CBasePlayerWeapon", "m_pReserveAmmo");
    RESOLVE_FIELD(scene_node_offset, "CBodyComponent", "m_pSceneNode");
    RESOLVE_FIELD(abs_origin_offset, "CGameSceneNode", "m_vecAbsOrigin");
#undef RESOLVE_FIELD

    impl_->give_named_item_index = *give_index;
    impl_->remove_weapons_index = *remove_all_index;
    impl_->drop_active_weapon_index = *drop_active_index;
    impl_->drop_weapon_index = *drop_weapon_index;
    impl_->select_weapon_index = *select_weapon_index;
    impl_->teleport_index = *teleport_index;
    impl_->commit_suicide_index = *suicide_index;
    impl_->respawn_index = *respawn_index;
    impl_->change_team_index = *change_team_index;

    const HMODULE server_module = GetModuleHandleW(L"server.dll");
    if (!server_module) {
        error = "server.dll is not loaded";
        return false;
    }
    impl_->client_print = reinterpret_cast<LuaCSGameApiImpl::ClientPrintFn>(
        luacs_game_internal::find_pattern(server_module,
                                          *client_print_pattern));
    impl_->client_print_all =
        reinterpret_cast<LuaCSGameApiImpl::ClientPrintAllFn>(
            luacs_game_internal::find_pattern(server_module,
                                              *client_print_all_pattern));
    impl_->remove_player_item =
        reinterpret_cast<LuaCSGameApiImpl::RemovePlayerItemFn>(
            luacs_game_internal::find_pattern(server_module,
                                              *remove_player_item_pattern));
    impl_->remove_entity = reinterpret_cast<LuaCSGameApiImpl::RemoveEntityFn>(
        luacs_game_internal::find_pattern(server_module,
                                          *remove_entity_pattern));
    impl_->switch_team = reinterpret_cast<LuaCSGameApiImpl::SwitchTeamFn>(
        luacs_game_internal::find_pattern(server_module,
                                          *switch_team_pattern));
    impl_->set_pawn = reinterpret_cast<LuaCSGameApiImpl::SetPawnFn>(
        luacs_game_internal::find_pattern(server_module, *set_pawn_pattern));
    impl_->event_manager_vtable =
        luacs_game_internal::find_virtual_table(server_module,
                                                "CGameEventManager");

    if (!impl_->client_print || !impl_->client_print_all ||
        !impl_->remove_player_item || !impl_->remove_entity ||
        !impl_->switch_team || !impl_->set_pawn ||
        !impl_->event_manager_vtable) {
        error = "could not resolve one or more required CS2 functions or the CGameEventManager vtable";
        return false;
    }

    if (!impl_->current_entity_system()) {
        error = "GameEntitySystem is not ready during Metamod Load; live player and inventory calls will become available after map startup";
    }
    return true;
}

void LuaCSGameApi::shutdown() {
    if (impl_ && g_pCVar == impl_->cvars) g_pCVar = nullptr;
    impl_ = std::make_unique<LuaCSGameApiImpl>();
}

IGameEventManager2* LuaCSGameApi::event_manager() const {
    return impl_ ? impl_->event_manager : nullptr;
}

void LuaCSGameApi::set_event_manager(IGameEventManager2* manager) {
    if (impl_) impl_->event_manager = manager;
}

void* LuaCSGameApi::event_manager_vtable() const {
    return impl_ ? impl_->event_manager_vtable : nullptr;
}

std::uint64_t LuaCSGameApi::begin_event(IGameEvent* event, bool post,
                                        bool dont_broadcast) {
    if (!impl_ || !event) return 0;
    const std::uint64_t token = impl_->next_event_token++;
    impl_->event_contexts.emplace(
        token, LuaCSGameApiImpl::EventContext{event, post, false,
                                              dont_broadcast});
    return token;
}

LuaCSEventDecision LuaCSGameApi::end_event(std::uint64_t token) {
    LuaCSEventDecision result{};
    if (!impl_ || token == 0) return result;
    const auto found = impl_->event_contexts.find(token);
    if (found == impl_->event_contexts.end()) return result;
    result.cancelled = found->second.cancelled;
    result.dont_broadcast = found->second.dont_broadcast;
    impl_->event_contexts.erase(found);
    return result;
}

luacs::HostOperations LuaCSGameApi::host_operations() {
    luacs::HostOperations operations;

    operations.event_has_key = [this](std::uint64_t token,
                                      std::string_view key) {
        auto* context = impl_->event_context(token);
        if (!context || !context->event) return false;
        const std::string owned(key);
        return context->event->HasKey(owned.c_str());
    };
    operations.event_is_empty = [this](std::uint64_t token,
                                       std::string_view key) {
        auto* context = impl_->event_context(token);
        if (!context || !context->event) return true;
        const std::string owned(key);
        return context->event->IsEmpty(owned.c_str());
    };
    operations.event_get_bool = [this](std::uint64_t token,
                                       std::string_view key, bool fallback,
                                       bool& output) {
        auto* context = impl_->event_context(token);
        if (!context || !context->event) return false;
        const std::string owned(key);
        output = context->event->GetBool(owned.c_str(), fallback);
        return true;
    };
    operations.event_get_int = [this](std::uint64_t token,
                                      std::string_view key, int fallback,
                                      int& output) {
        auto* context = impl_->event_context(token);
        if (!context || !context->event) return false;
        const std::string owned(key);
        output = context->event->GetInt(owned.c_str(), fallback);
        return true;
    };
    operations.event_get_uint64 = [this](std::uint64_t token,
                                         std::string_view key,
                                         std::uint64_t fallback,
                                         std::uint64_t& output) {
        auto* context = impl_->event_context(token);
        if (!context || !context->event) return false;
        const std::string owned(key);
        output = context->event->GetUint64(owned.c_str(), fallback);
        return true;
    };
    operations.event_get_float = [this](std::uint64_t token,
                                        std::string_view key, float fallback,
                                        float& output) {
        auto* context = impl_->event_context(token);
        if (!context || !context->event) return false;
        const std::string owned(key);
        output = context->event->GetFloat(owned.c_str(), fallback);
        return true;
    };
    operations.event_get_string = [this](std::uint64_t token,
                                         std::string_view key,
                                         std::string_view fallback,
                                         std::string& output) {
        auto* context = impl_->event_context(token);
        if (!context || !context->event) return false;
        const std::string owned_key(key);
        const std::string owned_fallback(fallback);
        output = context->event->GetString(owned_key.c_str(),
                                           owned_fallback.c_str());
        return true;
    };
    operations.event_get_player_slot = [this](std::uint64_t token,
                                              std::string_view key,
                                              int& output) {
        auto* context = impl_->event_context(token);
        if (!context || !context->event) return false;
        const std::string owned(key);
        output = context->event->GetPlayerSlot(owned.c_str()).Get();
        return true;
    };
    operations.event_get_entity_index = [this](std::uint64_t token,
                                               std::string_view key,
                                               int& output) {
        auto* context = impl_->event_context(token);
        if (!context || !context->event) return false;
        const std::string owned(key);
        output = context->event->GetEntityIndex(owned.c_str()).Get();
        return true;
    };
    operations.event_get_pawn_index = [this](std::uint64_t token,
                                             std::string_view key,
                                             int& output) {
        auto* context = impl_->event_context(token);
        if (!context || !context->event) return false;
        const std::string owned(key);
        output = context->event->GetPawnEntityIndex(owned.c_str()).Get();
        return true;
    };
    operations.event_set_bool = [this](std::uint64_t token,
                                       std::string_view key, bool value) {
        auto* context = impl_->event_context(token);
        if (!context || !context->event) return false;
        const std::string owned(key);
        context->event->SetBool(owned.c_str(), value);
        return true;
    };
    operations.event_set_int = [this](std::uint64_t token,
                                      std::string_view key, int value) {
        auto* context = impl_->event_context(token);
        if (!context || !context->event) return false;
        const std::string owned(key);
        context->event->SetInt(owned.c_str(), value);
        return true;
    };
    operations.event_set_uint64 = [this](std::uint64_t token,
                                         std::string_view key,
                                         std::uint64_t value) {
        auto* context = impl_->event_context(token);
        if (!context || !context->event) return false;
        const std::string owned(key);
        context->event->SetUint64(owned.c_str(), value);
        return true;
    };
    operations.event_set_float = [this](std::uint64_t token,
                                        std::string_view key, float value) {
        auto* context = impl_->event_context(token);
        if (!context || !context->event) return false;
        const std::string owned(key);
        context->event->SetFloat(owned.c_str(), value);
        return true;
    };
    operations.event_set_string = [this](std::uint64_t token,
                                         std::string_view key,
                                         std::string_view value) {
        auto* context = impl_->event_context(token);
        if (!context || !context->event) return false;
        const std::string owned_key(key);
        const std::string owned_value(value);
        context->event->SetString(owned_key.c_str(), owned_value.c_str());
        return true;
    };
    operations.event_cancel = [this](std::uint64_t token) {
        auto* context = impl_->event_context(token);
        if (!context || context->post) return false;
        context->cancelled = true;
        return true;
    };
    operations.event_set_dont_broadcast = [this](std::uint64_t token,
                                                 bool value) {
        auto* context = impl_->event_context(token);
        if (!context || context->post) return false;
        context->dont_broadcast = value;
        return true;
    };

    operations.player_state = [this](int slot, luacs::PlayerState& output,
                                     std::string& error) {
        return impl_->player_state(slot, output, error);
    };
    operations.player_set_int = [this](int slot, luacs::PlayerIntField field,
                                       int value, std::string& error) {
        return impl_->player_set_int(slot, field, value, error);
    };
    operations.player_set_bool = [this](int slot, luacs::PlayerBoolField field,
                                        bool value, std::string& error) {
        return impl_->player_set_bool(slot, field, value, error);
    };
    operations.player_teleport = [this](int slot,
                                        const luacs::Vector3* position,
                                        const luacs::Vector3* angles,
                                        const luacs::Vector3* velocity,
                                        std::string& error) {
        return impl_->player_teleport(slot, position, angles, velocity, error);
    };
    operations.player_kill = [this](int slot, bool explode,
                                    std::string& error) {
        return impl_->player_kill(slot, explode, error);
    };
    operations.player_respawn = [this](int slot, std::string& error) {
        return impl_->player_respawn(slot, error);
    };
    operations.player_change_team = [this](int slot, int team,
                                           bool use_switch,
                                           std::string& error) {
        return impl_->player_change_team(slot, team, use_switch, error);
    };

    operations.hud_print = [this](int slot, int destination,
                                  std::string_view message,
                                  std::string& error) {
        return impl_->hud_print(slot, destination, message, error);
    };
    operations.cvar_exists =
        [this](std::string_view name) { return impl_->cvar_exists(name); };
    operations.cvar_get = [this](std::string_view name, std::string& value,
                                 std::string& error) {
        return impl_->cvar_get(name, value, error);
    };
    operations.cvar_set = [this](std::string_view name,
                                 std::string_view value,
                                 std::string& error) {
        return impl_->cvar_set(name, value, error);
    };

    operations.weapon_give = [this](int slot, std::string_view classname,
                                    luacs::WeaponInfo& output,
                                    std::string& error) {
        return impl_->weapon_give(slot, classname, output, error);
    };
    operations.weapon_remove_all = [this](int slot, std::string& error) {
        return impl_->weapon_remove_all(slot, error);
    };
    operations.weapon_drop_active = [this](int slot, std::string& error) {
        return impl_->weapon_drop_active(slot, error);
    };
    operations.weapon_count = [this](int slot, std::string& error) {
        return impl_->weapon_count(slot, error);
    };
    operations.weapon_at = [this](int slot, std::size_t index,
                                  luacs::WeaponInfo& output,
                                  std::string& error) {
        return impl_->weapon_at(slot, index, output, error);
    };
    operations.weapon_get = [this](int entity_index,
                                   luacs::WeaponInfo& output,
                                   std::string& error) {
        return impl_->weapon_get(entity_index, output, error);
    };
    operations.weapon_remove = [this](int slot, int entity_index,
                                      bool delete_entity,
                                      std::string& error) {
        return impl_->weapon_remove(slot, entity_index, delete_entity, error);
    };
    operations.weapon_drop = [this](int slot, int entity_index,
                                    std::string& error) {
        return impl_->weapon_drop(slot, entity_index, error);
    };
    operations.weapon_switch = [this](int slot, int entity_index,
                                      std::string& error) {
        return impl_->weapon_switch(slot, entity_index, error);
    };
    operations.weapon_set_clip = [this](int entity_index, int clip_index,
                                        int value, std::string& error) {
        return impl_->weapon_set_clip(entity_index, clip_index, value, error);
    };
    operations.weapon_set_reserve =
        [this](int entity_index, int reserve_index, int value,
               std::string& error) {
            return impl_->weapon_set_reserve(entity_index, reserve_index, value,
                                             error);
        };
    operations.weapon_get_ammo = [this](int slot, int ammo_type, int& output,
                                        std::string& error) {
        return impl_->weapon_get_ammo(slot, ammo_type, output, error);
    };
    operations.weapon_set_ammo = [this](int slot, int ammo_type, int value,
                                        std::string& error) {
        return impl_->weapon_set_ammo(slot, ammo_type, value, error);
    };

    return operations;
}
