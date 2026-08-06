#include "game_api.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <entity2/entitysystem.h>
#include <entityhandle.h>
#include <icvar.h>
#include <schemasystem/schemasystem.h>
#include <tier1/bufferstring.h>
#include <tier1/convar.h>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <optional>
#include <regex>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace {

using FactoryFn = void*(__cdecl*)(const char*, int*);

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

std::optional<std::string> entry_windows_value(const std::string& json,
                                                std::string_view entry_name) {
    const std::string needle = "\"" + std::string(entry_name) + "\"";
    const auto position = json.find(needle);
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

bool valid_slot(int slot) { return slot >= 0 && slot < 64; }

bool valid_destination(int destination) {
    return destination == static_cast<int>(luacs::HudDestination::Notify) ||
           destination == static_cast<int>(luacs::HudDestination::Console) ||
           destination == static_cast<int>(luacs::HudDestination::Chat) ||
           destination == static_cast<int>(luacs::HudDestination::Center) ||
           destination == static_cast<int>(luacs::HudDestination::Alert);
}

} // namespace

struct LuaCSGameApi::Impl {
    using ClientPrintFn = void(__fastcall*)(void*, int, const char*, void*, void*,
                                            void*, void*);
    using ClientPrintAllFn = void(__fastcall*)(int, const char*, void*, void*, void*,
                                               void*, void*);
    using GiveNamedItemFn = void*(__fastcall*)(void*, const char*);
    using RemoveWeaponsFn = void(__fastcall*)(void*);
    using DropActiveWeaponFn = void(__fastcall*)(void*, void*);

    CGameEntitySystem* entity_system{};
    CSchemaSystem* schema_system{};
    CSchemaSystemTypeScope* server_scope{};
    ICvar* cvars{};
    ClientPrintFn client_print{};
    ClientPrintAllFn client_print_all{};
    std::size_t give_named_item_index{};
    std::size_t remove_weapons_index{};
    std::size_t drop_active_weapon_index{};
    std::ptrdiff_t player_pawn_offset{};
    std::ptrdiff_t item_services_offset{};
    std::ptrdiff_t weapon_services_offset{};
    std::ptrdiff_t active_weapon_offset{};

    CEntityInstance* entity_by_index(int index) const {
        if (!entity_system || index < 0 || index >= MAX_TOTAL_ENTITIES) return nullptr;
        const int chunk_index = index / MAX_ENTITIES_IN_LIST;
        const int entry_index = index % MAX_ENTITIES_IN_LIST;
        CEntityIdentity* chunk = entity_system->m_EntityList.m_pIdentityChunks[chunk_index];
        if (!chunk) return nullptr;
        CEntityIdentity* identity = &chunk[entry_index];
        if (!identity->m_pInstance || identity->m_EHandle.GetEntryIndex() != index) {
            return nullptr;
        }
        return identity->m_pInstance;
    }

    CEntityInstance* entity_by_handle(const CEntityHandle& handle) const {
        if (!handle.IsValid()) return nullptr;
        const int index = handle.GetEntryIndex();
        CEntityInstance* instance = entity_by_index(index);
        if (!instance) return nullptr;
        CEntityIdentity* identity = instance->m_pEntity;
        if (!identity || identity->GetRefEHandle() != handle) return nullptr;
        return instance;
    }

    CEntityInstance* controller(int slot, std::string& error) const {
        if (!valid_slot(slot)) {
            error = "player slot must be between 0 and 63";
            return nullptr;
        }
        CEntityInstance* result = entity_by_index(slot + 1);
        if (!result) error = "no live player controller exists for slot " + std::to_string(slot);
        return result;
    }

    CEntityInstance* pawn(int slot, std::string& error) const {
        CEntityInstance* player_controller = controller(slot, error);
        if (!player_controller) return nullptr;
        const auto* handle = reinterpret_cast<const CEntityHandle*>(
            reinterpret_cast<const std::uint8_t*>(player_controller) + player_pawn_offset);
        CEntityInstance* result = entity_by_handle(*handle);
        if (!result) error = "player slot " + std::to_string(slot) + " has no live pawn";
        return result;
    }

    void* service_pointer(CEntityInstance* player_pawn, std::ptrdiff_t offset,
                          const char* service_name, std::string& error) const {
        void* result = *reinterpret_cast<void**>(
            reinterpret_cast<std::uint8_t*>(player_pawn) + offset);
        if (!result) error = std::string("player pawn has no ") + service_name;
        return result;
    }

    template <typename Function>
    static Function virtual_function(void* object, std::size_t index) {
        if (!object) return nullptr;
        auto** table = *reinterpret_cast<void***>(object);
        return table ? reinterpret_cast<Function>(table[index]) : nullptr;
    }

    bool hud_print(int slot, int destination, std::string_view message,
                   std::string& error) const {
        if (!valid_destination(destination)) {
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

    bool cvar_exists(std::string_view name) const {
        if (!cvars || name.empty()) return false;
        const std::string owned(name);
        ConVarRefAbstract reference(owned.c_str());
        return reference.IsValidRef() && reference.IsConVarDataValid();
    }

    bool cvar_get(std::string_view name, std::string& value,
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

    bool cvar_set(std::string_view name, std::string_view value,
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

    bool weapon_give(int slot, std::string_view class_name,
                     std::string& error) const {
        CEntityInstance* player_pawn = pawn(slot, error);
        if (!player_pawn) return false;
        void* item_services = service_pointer(player_pawn, item_services_offset,
                                              "item services", error);
        if (!item_services) return false;
        const auto function = virtual_function<GiveNamedItemFn>(
            item_services, give_named_item_index);
        if (!function) {
            error = "GiveNamedItem virtual function is unavailable";
            return false;
        }
        const std::string owned(class_name);
        if (!function(item_services, owned.c_str())) {
            error = "CS2 could not create item '" + owned + "'";
            return false;
        }
        return true;
    }

    bool weapon_remove_all(int slot, std::string& error) const {
        CEntityInstance* player_pawn = pawn(slot, error);
        if (!player_pawn) return false;
        void* item_services = service_pointer(player_pawn, item_services_offset,
                                              "item services", error);
        if (!item_services) return false;
        const auto function = virtual_function<RemoveWeaponsFn>(
            item_services, remove_weapons_index);
        if (!function) {
            error = "RemoveWeapons virtual function is unavailable";
            return false;
        }
        function(item_services);
        return true;
    }

    bool weapon_drop_active(int slot, std::string& error) const {
        CEntityInstance* player_pawn = pawn(slot, error);
        if (!player_pawn) return false;
        void* item_services = service_pointer(player_pawn, item_services_offset,
                                              "item services", error);
        if (!item_services) return false;
        void* weapon_services = service_pointer(player_pawn, weapon_services_offset,
                                                "weapon services", error);
        if (!weapon_services) return false;
        const auto* active_handle = reinterpret_cast<const CEntityHandle*>(
            reinterpret_cast<const std::uint8_t*>(weapon_services) + active_weapon_offset);
        CEntityInstance* active_weapon = entity_by_handle(*active_handle);
        if (!active_weapon) {
            error = "player has no active weapon to drop";
            return false;
        }
        const auto function = virtual_function<DropActiveWeaponFn>(
            item_services, drop_active_weapon_index);
        if (!function) {
            error = "DropActivePlayerWeapon virtual function is unavailable";
            return false;
        }
        function(item_services, active_weapon);
        return true;
    }
};

LuaCSGameApi::LuaCSGameApi() : impl_(std::make_unique<Impl>()) {}
LuaCSGameApi::~LuaCSGameApi() = default;

bool LuaCSGameApi::initialize(const std::filesystem::path& luacs_root,
                              std::string& error) {
    shutdown();
    impl_ = std::make_unique<Impl>();

    const auto gamedata_path =
        luacs_root / "gamedata" / "reference" / "official_windows_gamedata.json";
    const std::string gamedata = read_file(gamedata_path);
    if (gamedata.empty()) {
        error = "could not read official CS2 gamedata: " + gamedata_path.string();
        return false;
    }

    const auto game_entity_system_offset =
        entry_windows_number(gamedata, "GameEntitySystem");
    const auto give_index = entry_windows_number(
        gamedata, "CCSPlayer_ItemServices_GiveNamedItem");
    const auto remove_index = entry_windows_number(
        gamedata, "CCSPlayer_ItemServices_RemoveWeapons");
    const auto drop_index = entry_windows_number(
        gamedata, "CCSPlayer_ItemServices_DropActivePlayerWeapon");
    const auto client_print_pattern = entry_windows_value(gamedata, "ClientPrint");
    const auto client_print_all_pattern =
        entry_windows_value(gamedata, "UTIL_ClientPrintAll");

    if (!game_entity_system_offset || !give_index || !remove_index || !drop_index ||
        !client_print_pattern || !client_print_all_pattern) {
        error = "official CS2 gamedata is missing a required weapons or HUD entry";
        return false;
    }

    const FactoryFn engine_factory = module_factory(L"engine2.dll");
    const FactoryFn schema_factory = module_factory(L"schemasystem.dll");
    const FactoryFn cvar_factory = module_factory(L"tier0.dll");
    if (!engine_factory || !schema_factory || !cvar_factory) {
        error = "could not resolve engine2, schemasystem, or tier0 CreateInterface";
        return false;
    }

    void* game_resource_service =
        engine_factory("GameResourceServiceServerV001", nullptr);
    impl_->schema_system = static_cast<CSchemaSystem*>(
        schema_factory(SCHEMASYSTEM_INTERFACE_VERSION, nullptr));
    impl_->cvars = static_cast<ICvar*>(cvar_factory("VEngineCvar007", nullptr));
    if (!game_resource_service || !impl_->schema_system || !impl_->cvars) {
        error = "could not acquire GameResourceServiceServerV001, SchemaSystem_001, or VEngineCvar007";
        return false;
    }
    g_pCVar = impl_->cvars;

    impl_->entity_system = *reinterpret_cast<CGameEntitySystem**>(
        reinterpret_cast<std::uint8_t*>(game_resource_service) +
        *game_entity_system_offset);
    if (!impl_->entity_system) {
        error = "GameEntitySystem pointer was null";
        return false;
    }

    impl_->server_scope = impl_->schema_system->FindTypeScopeForModule("server.dll");
    if (!impl_->server_scope) {
        error = "Source 2 schema scope for server.dll was unavailable";
        return false;
    }

    const auto player_pawn_offset = resolve_schema_field(
        impl_->server_scope, "CCSPlayerController", "m_hPlayerPawn");
    const auto item_services_offset = resolve_schema_field(
        impl_->server_scope, "CBasePlayerPawn", "m_pItemServices");
    const auto weapon_services_offset = resolve_schema_field(
        impl_->server_scope, "CBasePlayerPawn", "m_pWeaponServices");
    const auto active_weapon_offset = resolve_schema_field(
        impl_->server_scope, "CPlayer_WeaponServices", "m_hActiveWeapon");
    if (!player_pawn_offset || !item_services_offset || !weapon_services_offset ||
        !active_weapon_offset) {
        error = "Source 2 schema could not resolve the required player weapon fields";
        return false;
    }

    impl_->player_pawn_offset = *player_pawn_offset;
    impl_->item_services_offset = *item_services_offset;
    impl_->weapon_services_offset = *weapon_services_offset;
    impl_->active_weapon_offset = *active_weapon_offset;
    impl_->give_named_item_index = *give_index;
    impl_->remove_weapons_index = *remove_index;
    impl_->drop_active_weapon_index = *drop_index;

    const HMODULE server_module = GetModuleHandleW(L"server.dll");
    impl_->client_print = reinterpret_cast<Impl::ClientPrintFn>(
        find_pattern(server_module, *client_print_pattern));
    impl_->client_print_all = reinterpret_cast<Impl::ClientPrintAllFn>(
        find_pattern(server_module, *client_print_all_pattern));
    if (!impl_->client_print || !impl_->client_print_all) {
        error = "could not resolve ClientPrint or UTIL_ClientPrintAll from official gamedata";
        return false;
    }

    return true;
}

void LuaCSGameApi::shutdown() { impl_ = std::make_unique<Impl>(); }

luacs::HostOperations LuaCSGameApi::host_operations() {
    return {
        [this](int slot, int destination, std::string_view message,
               std::string& error) {
            return impl_->hud_print(slot, destination, message, error);
        },
        [this](std::string_view name) { return impl_->cvar_exists(name); },
        [this](std::string_view name, std::string& value, std::string& error) {
            return impl_->cvar_get(name, value, error);
        },
        [this](std::string_view name, std::string_view value, std::string& error) {
            return impl_->cvar_set(name, value, error);
        },
        [this](int slot, std::string_view class_name, std::string& error) {
            return impl_->weapon_give(slot, class_name, error);
        },
        [this](int slot, std::string& error) {
            return impl_->weapon_remove_all(slot, error);
        },
        [this](int slot, std::string& error) {
            return impl_->weapon_drop_active(slot, error);
        },
    };
}
