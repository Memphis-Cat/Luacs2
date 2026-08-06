#include "game_api_internal.h"
#include "luacs/advanced_world_api.h"
#include "luacs/world_api.h"

#include <gametrace.h>
#include <schemasystem/schemasystem.h>
#include <schemasystem/schematypes.h>
#include <tier1/utlstring.h>
#include <tier1/utlsymbollarge.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <optional>
#include <regex>
#include <string>
#include <string_view>
#include <vector>

extern "C" const luacs::WorldServices* LuaCS_GetWorldServices();

namespace {

using namespace luacs;
using namespace luacs_game_internal;

void copy_text(char* destination, std::size_t capacity,
               std::string_view value) {
    if (!destination || capacity == 0) return;
    std::snprintf(destination, capacity, "%.*s",
                  static_cast<int>(value.size()), value.data());
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

std::optional<std::string> windows_value(const std::string& json,
                                         std::string_view entry_name) {
    std::size_t position = json.find("\"" + std::string(entry_name) + "\"");
    if (position == std::string::npos) return std::nullopt;
    const auto maximum = std::min(json.size(), position + 4096);
    const std::string block = json.substr(position, maximum - position);
    const std::regex expression(
        "\"windows\"\\s*:\\s*\"([^\"]+)\"",
        std::regex::ECMAScript);
    std::smatch match;
    if (!std::regex_search(block, match, expression)) return std::nullopt;
    return match[1].str();
}

void* relative_address(void* instruction, std::size_t displacement_offset,
                       std::size_t instruction_size) {
    if (!instruction) return nullptr;
    const auto* bytes = static_cast<const std::uint8_t*>(instruction);
    const auto displacement = *reinterpret_cast<const std::int32_t*>(
        bytes + displacement_offset);
    return const_cast<std::uint8_t*>(bytes + instruction_size + displacement);
}

std::string type_name(CSchemaType* type) {
    if (!type) return {};
    const char* value = type->m_sTypeName.String();
    return value ? value : "";
}

bool has_network_metadata(const SchemaClassFieldData_t& field) {
    for (int index = 0; index < field.m_nStaticMetadataCount; ++index) {
        const char* name = field.m_pStaticMetadata[index].m_pszName;
        if (name && std::string_view(name).starts_with("MNetwork")) return true;
    }
    return false;
}

const SchemaClassFieldData_t* find_field(CSchemaClassInfo* class_info,
                                         std::string_view name,
                                         std::uint32_t& offset,
                                         bool& networked) {
    if (!class_info) return nullptr;
    for (std::uint16_t index = 0; index < class_info->m_nFieldCount; ++index) {
        const auto& field = class_info->m_pFields[index];
        if (field.m_pszName && name == field.m_pszName) {
            offset += static_cast<std::uint32_t>(field.m_nSingleInheritanceOffset);
            networked = networked || has_network_metadata(field);
            return &field;
        }
    }
    for (std::uint8_t index = 0; index < class_info->m_nBaseClassCount; ++index) {
        const auto& base = class_info->m_pBaseClasses[index];
        std::uint32_t inherited = offset + base.m_nOffset;
        bool inherited_networked = networked;
        const auto* result =
            find_field(base.m_pClass, name, inherited, inherited_networked);
        if (result) {
            offset = inherited;
            networked = inherited_networked;
            return result;
        }
    }
    return nullptr;
}

struct PathPart {
    std::string name;
    int index{-1};
};

bool parse_path(std::string_view path, std::vector<PathPart>& output,
                std::string& error) {
    if (path.empty()) {
        error = "property path cannot be empty";
        return false;
    }
    std::size_t begin = 0;
    while (begin < path.size()) {
        const std::size_t dot = path.find('.', begin);
        const std::string_view token = path.substr(
            begin, dot == std::string_view::npos ? path.size() - begin
                                                 : dot - begin);
        if (token.empty()) {
            error = "property path contains an empty segment";
            return false;
        }
        PathPart part;
        const std::size_t bracket = token.find('[');
        if (bracket == std::string_view::npos) {
            part.name.assign(token);
        } else {
            if (token.back() != ']' || bracket == 0) {
                error = "invalid property array syntax";
                return false;
            }
            part.name.assign(token.substr(0, bracket));
            const std::string number(token.substr(bracket + 1,
                                                   token.size() - bracket - 2));
            try {
                std::size_t parsed{};
                const long value = std::stol(number, &parsed, 10);
                if (parsed != number.size() || value < 0 ||
                    value > static_cast<long>(INT_MAX)) {
                    error = "property array index is invalid";
                    return false;
                }
                part.index = static_cast<int>(value);
            } catch (...) {
                error = "property array index is invalid";
                return false;
            }
        }
        output.push_back(std::move(part));
        if (dot == std::string_view::npos) break;
        begin = dot + 1;
    }
    return true;
}

PropertyKind classify(CSchemaType* type, std::uint8_t& width,
                      std::uint16_t& array_count,
                      std::uint16_t& element_size) {
    width = 0;
    array_count = 0;
    element_size = 0;
    if (!type) return PropertyKind::Invalid;

    if (type->m_eTypeCategory == SCHEMA_TYPE_BUILTIN) {
        const auto* builtin = static_cast<CSchemaType_Builtin*>(type);
        width = builtin->m_nSize;
        switch (builtin->m_eBuiltinType) {
            case SCHEMA_BUILTIN_TYPE_BOOL: return PropertyKind::Boolean;
            case SCHEMA_BUILTIN_TYPE_CHAR:
            case SCHEMA_BUILTIN_TYPE_INT8:
            case SCHEMA_BUILTIN_TYPE_INT16:
            case SCHEMA_BUILTIN_TYPE_INT32:
            case SCHEMA_BUILTIN_TYPE_INT64:
                return PropertyKind::SignedInteger;
            case SCHEMA_BUILTIN_TYPE_UINT8:
            case SCHEMA_BUILTIN_TYPE_UINT16:
            case SCHEMA_BUILTIN_TYPE_UINT32:
            case SCHEMA_BUILTIN_TYPE_UINT64:
                return PropertyKind::UnsignedInteger;
            case SCHEMA_BUILTIN_TYPE_FLOAT32:
            case SCHEMA_BUILTIN_TYPE_FLOAT64:
                return PropertyKind::Float;
            default: return PropertyKind::Invalid;
        }
    }

    if (type->m_eTypeCategory == SCHEMA_TYPE_DECLARED_ENUM) {
        const auto* declared = static_cast<CSchemaType_DeclaredEnum*>(type);
        width = declared->m_pEnumInfo ? declared->m_pEnumInfo->m_nSize : 4;
        return PropertyKind::SignedInteger;
    }

    if (type->m_eTypeCategory == SCHEMA_TYPE_POINTER) {
        width = static_cast<std::uint8_t>(sizeof(void*));
        return PropertyKind::Pointer;
    }

    if (type->m_eTypeCategory == SCHEMA_TYPE_BITFIELD) {
        const auto* bitfield = static_cast<CSchemaType_Bitfield*>(type);
        width = static_cast<std::uint8_t>(
            std::clamp((bitfield->m_nBitfieldCount + 7) / 8, 1, 8));
        return PropertyKind::UnsignedInteger;
    }

    if (type->m_eTypeCategory == SCHEMA_TYPE_FIXED_ARRAY) {
        const auto* array = static_cast<CSchemaType_FixedArray*>(type);
        array_count = static_cast<std::uint16_t>(
            std::clamp(array->m_nElementCount, 0, 65535));
        element_size = array->m_nElementSize;
        if (array->m_pElementType &&
            array->m_pElementType->m_eTypeCategory == SCHEMA_TYPE_BUILTIN &&
            static_cast<CSchemaType_Builtin*>(array->m_pElementType)
                    ->m_eBuiltinType == SCHEMA_BUILTIN_TYPE_CHAR) {
            width = static_cast<std::uint8_t>(1);
            return PropertyKind::String;
        }
        return classify(array->m_pElementType, width, array_count,
                        element_size);
    }

    const std::string name = type_name(type);
    if (name == "Vector" || name.find("Vector3") != std::string::npos) {
        width = 12;
        return PropertyKind::Vector;
    }
    if (name == "QAngle" || name.find("QAngle") != std::string::npos) {
        width = 12;
        return PropertyKind::Angle;
    }
    if (name.find("CEntityHandle") != std::string::npos ||
        name.find("CHandle<") != std::string::npos ||
        name.find("CNetworkHandle") != std::string::npos) {
        width = 4;
        return PropertyKind::EntityHandle;
    }
    if (name.find("CUtlString") != std::string::npos ||
        name.find("CUtlSymbolLarge") != std::string::npos) {
        width = static_cast<std::uint8_t>(sizeof(void*));
        return PropertyKind::String;
    }
    if (name.find("GameTime_t") != std::string::npos ||
        name.find("CNetworkedQuantizedFloat") != std::string::npos) {
        width = 4;
        return PropertyKind::Float;
    }
    if (type->m_eTypeCategory == SCHEMA_TYPE_ATOMIC &&
        type->m_eAtomicCategory == SCHEMA_ATOMIC_COLLECTION_OF_T) {
        const auto* collection =
            static_cast<CSchemaType_Atomic_CollectionOfT*>(type);
        element_size = collection->m_nElementSize;
        return classify(collection->m_pTemplateType, width, array_count,
                        element_size);
    }
    return PropertyKind::Invalid;
}

struct ResolvedProperty {
    CEntityInstance* entity{};
    void* address{};
    CSchemaType* type{};
    PropertyInfo info{};
    std::uint32_t root_offset{};
    int array_index{-1};
};

class LuaCSAdvancedApi {
public:
    using TraceSimpleFn = bool(__fastcall*)(void*, const Vector*, const Vector*,
                                             CEntityInstance*, std::uint64_t,
                                             std::uint64_t, CGameTrace*);

    struct NativeRay {
        std::array<std::byte, 40> data{};
        std::int32_t type{};
    };
    static_assert(sizeof(NativeRay) == 44);

    using TraceShapeFn = bool(__fastcall*)(void*, NativeRay*, const Vector*,
                                           const Vector*, CTraceFilter*,
                                           CGameTrace*);

    LuaCSAdvancedApi() {
        services.abi_version = kAdvancedWorldServicesAbiVersion;
        services.context = this;
        services.property_info = &property_info_bridge;
        services.property_get = &property_get_bridge;
        services.property_set = &property_set_bridge;
        services.property_count = &property_count_bridge;
        services.property_at = &property_at_bridge;
        services.trace = &trace_bridge;
        services.grenade_get = &grenade_get_bridge;
        services.grenade_count = &grenade_count_bridge;
        services.grenade_at = &grenade_at_bridge;
        services.grenade_spawn = &grenade_spawn_bridge;
        services.grenade_detonate = &grenade_detonate_bridge;
        services.grenade_remove = &grenade_remove_bridge;
    }

    bool initialize(std::string& error) {
        if (initialized_) return true;
        if (attempted_) {
            error = initialization_error_;
            return false;
        }
        attempted_ = true;

        const auto root = luacs_root();
        const std::string gamedata = read_file(
            root / "gamedata" / "reference" / "advanced_windows_gamedata.json");
        const auto manager_pattern = windows_value(gamedata, "GameTraceManager");
        const auto trace_pattern = windows_value(gamedata, "TraceFunc");
        const auto shape_pattern = windows_value(gamedata, "TraceShape");
        if (root.empty() || !manager_pattern || !trace_pattern || !shape_pattern) {
            return fail_init("advanced trace gamedata is unavailable", error);
        }

        const auto engine_factory = module_factory(L"engine2.dll");
        const auto schema_factory = module_factory(L"schemasystem.dll");
        if (!engine_factory || !schema_factory) {
            return fail_init("could not resolve advanced Source 2 factories", error);
        }
        game_resource_service_ =
            engine_factory("GameResourceServiceServerV001", nullptr);
        schema_system_ = static_cast<CSchemaSystem*>(
            schema_factory(SCHEMASYSTEM_INTERFACE_VERSION, nullptr));
        if (!game_resource_service_ || !schema_system_) {
            return fail_init("advanced Source 2 interfaces are unavailable", error);
        }
        scope_ = schema_system_->FindTypeScopeForModule("server.dll");
        if (!scope_) return fail_init("server.dll schema scope is unavailable", error);

        // This public offset is pinned with the same HL2SDK/gamedata revision as
        // the core plugin. The world service validates the entity system again
        // on every operation.
        const std::string official = read_file(
            root / "gamedata" / "reference" / "official_windows_gamedata.json");
        const std::regex entity_system_expression(
            "\"GameEntitySystem\"[\\s\\S]*?\"windows\"\\s*:\\s*([0-9]+)");
        std::smatch match;
        if (!std::regex_search(official, match, entity_system_expression)) {
            return fail_init("GameEntitySystem offset is unavailable", error);
        }
        entity_system_offset_ = static_cast<std::size_t>(
            std::stoull(match[1].str()));

        const HMODULE server = GetModuleHandleW(L"server.dll");
        if (!server) return fail_init("server.dll is not loaded", error);
        void* manager_instruction = find_pattern(server, *manager_pattern);
        trace_simple_ = reinterpret_cast<TraceSimpleFn>(
            find_pattern(server, *trace_pattern));
        trace_shape_ = reinterpret_cast<TraceShapeFn>(
            find_pattern(server, *shape_pattern));
        void* manager_storage = relative_address(manager_instruction, 3, 7);
        trace_manager_ = manager_storage
                             ? *reinterpret_cast<void**>(manager_storage)
                             : nullptr;
        if (!trace_manager_ || !trace_simple_ || !trace_shape_) {
            return fail_init("one or more trace functions could not be resolved",
                             error);
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

    bool ready(std::string& error) {
        if (!initialize(error)) return false;
        if (!entity_system()) {
            error = "GameEntitySystem is not ready; retry after map startup";
            return false;
        }
        return true;
    }

    bool resolve(int entity_index, std::string_view path, int explicit_index,
                 ResolvedProperty& output, std::string& error) {
        if (!ready(error)) return false;
        CEntityInstance* instance = entity(entity_index);
        if (!instance) {
            error = "entity index is invalid";
            return false;
        }
        CSchemaClassInfo* class_info = instance->Schema_DynamicBinding().Get();
        if (!class_info) {
            error = "entity has no dynamic schema binding";
            return false;
        }
        std::vector<PathPart> parts;
        if (!parse_path(path, parts, error)) return false;

        void* base = instance;
        std::uint32_t accumulated = 0;
        std::uint32_t root_offset = 0;
        bool networked = false;
        const SchemaClassFieldData_t* field = nullptr;
        CSchemaType* type = nullptr;
        int selected_index = explicit_index;

        for (std::size_t part_index = 0; part_index < parts.size(); ++part_index) {
            const auto& part = parts[part_index];
            std::uint32_t local = 0;
            bool field_networked = false;
            field = find_field(class_info, part.name, local, field_networked);
            if (!field) {
                error = "schema property was not found: " + part.name;
                return false;
            }
            if (part_index == 0) root_offset = local;
            accumulated += local;
            networked = networked || field_networked;
            type = field->m_pType;
            void* address = reinterpret_cast<std::uint8_t*>(base) + local;
            int index = part.index;
            if (part_index + 1 == parts.size() && selected_index >= 0) {
                if (index >= 0 && index != selected_index) {
                    error = "property path and argument specify different array indexes";
                    return false;
                }
                index = selected_index;
            }

            if (index >= 0) {
                if (type->m_eTypeCategory == SCHEMA_TYPE_FIXED_ARRAY) {
                    auto* array = static_cast<CSchemaType_FixedArray*>(type);
                    if (index >= array->m_nElementCount) {
                        error = "fixed-array index is out of range";
                        return false;
                    }
                    address = reinterpret_cast<std::uint8_t*>(address) +
                              static_cast<std::size_t>(index) *
                                  array->m_nElementSize;
                    type = array->m_pElementType;
                } else if (type->m_eTypeCategory == SCHEMA_TYPE_ATOMIC &&
                           type->m_eAtomicCategory ==
                               SCHEMA_ATOMIC_COLLECTION_OF_T) {
                    auto* collection =
                        static_cast<CSchemaType_Atomic_CollectionOfT*>(type);
                    if (!collection->m_pfnManipulator) {
                        error = "schema collection has no manipulator";
                        return false;
                    }
                    const auto count = reinterpret_cast<std::uintptr_t>(
                        collection->m_pfnManipulator(
                            SCHEMA_COLLECTION_MANIPULATOR_ACTION_GET_COUNT,
                            address, 0, 0));
                    if (index >= static_cast<int>(count)) {
                        error = "schema collection index is out of range";
                        return false;
                    }
                    address = collection->m_pfnManipulator(
                        SCHEMA_COLLECTION_MANIPULATOR_ACTION_GET_ELEMENT,
                        address, index, 0);
                    type = collection->m_pTemplateType;
                } else {
                    error = "property is not an indexable schema array";
                    return false;
                }
                selected_index = index;
            }

            if (part_index + 1 < parts.size()) {
                if (type->m_eTypeCategory == SCHEMA_TYPE_POINTER) {
                    auto* pointer_type = static_cast<CSchemaType_Ptr*>(type);
                    base = *reinterpret_cast<void**>(address);
                    type = pointer_type->m_pObjectType;
                    if (!base) {
                        error = "nested schema pointer is null";
                        return false;
                    }
                } else {
                    base = address;
                }
                if (!type || type->m_eTypeCategory != SCHEMA_TYPE_DECLARED_CLASS) {
                    error = "property path descends through a non-class field";
                    return false;
                }
                class_info =
                    static_cast<CSchemaType_DeclaredClass*>(type)->m_pClassInfo;
                accumulated = 0;
                continue;
            }

            output = {};
            output.entity = instance;
            output.address = address;
            output.type = type;
            output.root_offset = root_offset;
            output.array_index = selected_index;
            output.info.valid = true;
            output.info.networked = networked;
            output.info.offset = accumulated;
            output.info.kind = classify(type, output.info.element_size,
                                        output.info.array_count,
                                        output.info.element_size);
            output.info.writable = output.info.kind != PropertyKind::Invalid;
            copy_text(output.info.name, sizeof(output.info.name), path);
            copy_text(output.info.type_name, sizeof(output.info.type_name),
                      type_name(type));
            return true;
        }
        error = "property resolution failed";
        return false;
    }

    bool property_info(int entity_index, std::string_view path,
                       PropertyInfo& output, std::string& error) {
        ResolvedProperty property;
        if (!resolve(entity_index, path, -1, property, error)) return false;
        output = property.info;
        return true;
    }

    template <typename T>
    static T read_scalar(const void* address) {
        return *reinterpret_cast<const T*>(address);
    }

    template <typename T>
    static void write_scalar(void* address, T value) {
        *reinterpret_cast<T*>(address) = value;
    }

    bool property_get(int entity_index, std::string_view path, int array_index,
                      PropertyValue& output, std::string& error) {
        ResolvedProperty property;
        if (!resolve(entity_index, path, array_index, property, error)) return false;
        output = {};
        output.kind = property.info.kind;
        output.width = property.info.element_size;
        const auto width = property.info.element_size;
        switch (output.kind) {
            case PropertyKind::Boolean:
                output.boolean_value = read_scalar<bool>(property.address);
                break;
            case PropertyKind::SignedInteger:
                switch (width) {
                    case 1: output.signed_value = read_scalar<std::int8_t>(property.address); break;
                    case 2: output.signed_value = read_scalar<std::int16_t>(property.address); break;
                    case 4: output.signed_value = read_scalar<std::int32_t>(property.address); break;
                    case 8: output.signed_value = read_scalar<std::int64_t>(property.address); break;
                    default: error = "unsupported signed integer width"; return false;
                }
                break;
            case PropertyKind::UnsignedInteger:
                switch (width) {
                    case 1: output.unsigned_value = read_scalar<std::uint8_t>(property.address); break;
                    case 2: output.unsigned_value = read_scalar<std::uint16_t>(property.address); break;
                    case 4: output.unsigned_value = read_scalar<std::uint32_t>(property.address); break;
                    case 8: output.unsigned_value = read_scalar<std::uint64_t>(property.address); break;
                    default: error = "unsupported unsigned integer width"; return false;
                }
                break;
            case PropertyKind::Float:
                output.float_value = width == 8
                                         ? read_scalar<double>(property.address)
                                         : read_scalar<float>(property.address);
                break;
            case PropertyKind::Vector: {
                const Vector& value = read_scalar<Vector>(property.address);
                output.vector_value = {value.x, value.y, value.z};
                break;
            }
            case PropertyKind::Angle: {
                const QAngle& value = read_scalar<QAngle>(property.address);
                output.vector_value = {value.x, value.y, value.z};
                break;
            }
            case PropertyKind::EntityHandle: {
                const CEntityHandle handle =
                    read_scalar<CEntityHandle>(property.address);
                output.entity_handle = handle.ToInt();
                output.entity_index = handle.IsValid() ? handle.GetEntryIndex() : -1;
                break;
            }
            case PropertyKind::Pointer:
                output.unsigned_value = reinterpret_cast<std::uintptr_t>(
                    read_scalar<void*>(property.address));
                break;
            case PropertyKind::String: {
                const std::string name = type_name(property.type);
                if (property.type->m_eTypeCategory == SCHEMA_TYPE_FIXED_ARRAY) {
                    copy_text(output.string_value, sizeof(output.string_value),
                              static_cast<const char*>(property.address));
                } else if (name.find("CUtlSymbolLarge") != std::string::npos) {
                    const char* value =
                        read_scalar<CUtlSymbolLarge>(property.address).String();
                    copy_text(output.string_value, sizeof(output.string_value),
                              value ? value : "");
                } else {
                    const char* value =
                        read_scalar<CUtlString>(property.address).String();
                    copy_text(output.string_value, sizeof(output.string_value),
                              value ? value : "");
                }
                break;
            }
            default:
                error = "schema property type is not readable";
                return false;
        }
        return true;
    }

    bool property_set(int entity_index, std::string_view path, int array_index,
                      const PropertyValue& value, bool network,
                      std::string& error) {
        ResolvedProperty property;
        if (!resolve(entity_index, path, array_index, property, error)) return false;
        if (!property.info.writable || property.info.kind != value.kind) {
            error = "property value type does not match schema type";
            return false;
        }
        const auto width = property.info.element_size;
        switch (value.kind) {
            case PropertyKind::Boolean:
                write_scalar(property.address, value.boolean_value);
                break;
            case PropertyKind::SignedInteger:
                switch (width) {
                    case 1: write_scalar(property.address, static_cast<std::int8_t>(value.signed_value)); break;
                    case 2: write_scalar(property.address, static_cast<std::int16_t>(value.signed_value)); break;
                    case 4: write_scalar(property.address, static_cast<std::int32_t>(value.signed_value)); break;
                    case 8: write_scalar(property.address, value.signed_value); break;
                    default: error = "unsupported signed integer width"; return false;
                }
                break;
            case PropertyKind::UnsignedInteger:
                switch (width) {
                    case 1: write_scalar(property.address, static_cast<std::uint8_t>(value.unsigned_value)); break;
                    case 2: write_scalar(property.address, static_cast<std::uint16_t>(value.unsigned_value)); break;
                    case 4: write_scalar(property.address, static_cast<std::uint32_t>(value.unsigned_value)); break;
                    case 8: write_scalar(property.address, value.unsigned_value); break;
                    default: error = "unsupported unsigned integer width"; return false;
                }
                break;
            case PropertyKind::Float:
                if (width == 8) write_scalar(property.address, value.float_value);
                else write_scalar(property.address, static_cast<float>(value.float_value));
                break;
            case PropertyKind::Vector:
                write_scalar(property.address,
                             Vector(value.vector_value.x, value.vector_value.y,
                                    value.vector_value.z));
                break;
            case PropertyKind::Angle:
                write_scalar(property.address,
                             QAngle(value.vector_value.x, value.vector_value.y,
                                    value.vector_value.z));
                break;
            case PropertyKind::EntityHandle: {
                CEntityHandle handle;
                if (value.entity_index >= 0) handle.Set(entity(value.entity_index));
                else handle.Term();
                write_scalar(property.address, handle);
                break;
            }
            case PropertyKind::Pointer:
                write_scalar(property.address,
                             reinterpret_cast<void*>(
                                 static_cast<std::uintptr_t>(value.unsigned_value)));
                break;
            case PropertyKind::String: {
                const std::string name = type_name(property.type);
                if (property.type->m_eTypeCategory == SCHEMA_TYPE_FIXED_ARRAY) {
                    auto* array = static_cast<CSchemaType_FixedArray*>(property.type);
                    const std::size_t capacity = static_cast<std::size_t>(
                        std::max(array->m_nElementCount, 0));
                    if (!capacity) {
                        error = "fixed string has zero capacity";
                        return false;
                    }
                    std::snprintf(static_cast<char*>(property.address), capacity,
                                  "%s", value.string_value);
                } else if (name.find("CUtlSymbolLarge") != std::string::npos) {
                    *reinterpret_cast<CUtlSymbolLarge*>(property.address) =
                        CUtlSymbolLarge(value.string_value);
                } else {
                    *reinterpret_cast<CUtlString*>(property.address) =
                        value.string_value;
                }
                break;
            }
            default:
                error = "schema property type is not writable";
                return false;
        }
        if (network) {
            property.entity->NetworkStateChanged(NetworkStateChangedData(
                property.root_offset, property.array_index));
        }
        return true;
    }

    void enumerate_class(CSchemaClassInfo* class_info, bool inherited,
                         std::vector<PropertyInfo>& output,
                         std::uint32_t base_offset = 0) {
        if (!class_info) return;
        for (std::uint16_t index = 0; index < class_info->m_nFieldCount; ++index) {
            const auto& field = class_info->m_pFields[index];
            PropertyInfo info;
            info.valid = true;
            info.networked = has_network_metadata(field);
            info.offset = base_offset +
                          static_cast<std::uint32_t>(field.m_nSingleInheritanceOffset);
            info.kind = classify(field.m_pType, info.element_size,
                                 info.array_count, info.element_size);
            info.writable = info.kind != PropertyKind::Invalid;
            copy_text(info.name, sizeof(info.name),
                      field.m_pszName ? field.m_pszName : "");
            copy_text(info.type_name, sizeof(info.type_name),
                      type_name(field.m_pType));
            output.push_back(info);
        }
        if (!inherited) return;
        for (std::uint8_t index = 0; index < class_info->m_nBaseClassCount; ++index) {
            const auto& base = class_info->m_pBaseClasses[index];
            enumerate_class(base.m_pClass, true, output,
                            base_offset + base.m_nOffset);
        }
    }

    std::vector<PropertyInfo> property_list(int entity_index, bool inherited,
                                            std::string& error) {
        std::vector<PropertyInfo> result;
        if (!ready(error)) return result;
        CEntityInstance* instance = entity(entity_index);
        if (!instance) {
            error = "entity index is invalid";
            return result;
        }
        CSchemaClassInfo* binding = instance->Schema_DynamicBinding().Get();
        if (!binding) {
            error = "entity has no dynamic schema binding";
            return result;
        }
        enumerate_class(binding, inherited, result);
        return result;
    }

    bool trace(const TraceRequest& request, TraceResult& output,
               std::string& error) {
        if (!ready(error)) return false;
        const Vector start(request.start.x, request.start.y, request.start.z);
        const Vector end(request.end.x, request.end.y, request.end.z);
        CGameTrace result;
        bool traced{};
        if (!request.use_hull) {
            traced = trace_simple_(trace_manager_, &start, &end,
                                   entity(request.ignore_entity_index),
                                   request.contents_mask,
                                   request.collision_group, &result);
        } else {
            NativeRay ray;
            auto* bounds = reinterpret_cast<Vector*>(ray.data.data());
            bounds[0] = Vector(request.mins.x, request.mins.y, request.mins.z);
            bounds[1] = Vector(request.maxs.x, request.maxs.y, request.maxs.z);
            ray.type = 2;
            CTraceFilter filter(request.contents_mask,
                                static_cast<int>(request.collision_group), true);
            filter.SetPassEntity1(entity(request.ignore_entity_index));
            filter.m_bHitTrigger = request.hit_triggers;
            traced = trace_shape_(trace_manager_, &ray, &start, &end, &filter,
                                  &result);
        }
        if (!traced) {
            error = "CS2 trace function rejected the request";
            return false;
        }
        output = {};
        output.valid = true;
        output.hit = result.DidHit();
        output.start_solid = result.m_bStartInSolid;
        output.all_solid = result.m_bStartInSolid && result.m_flFraction <= 0.0f;
        output.fraction = result.m_flFraction;
        output.start = {result.m_vStartPos.x, result.m_vStartPos.y,
                        result.m_vStartPos.z};
        output.end = {result.m_vEndPos.x, result.m_vEndPos.y,
                      result.m_vEndPos.z};
        output.hit_position = {result.m_vHitPoint.x, result.m_vHitPoint.y,
                               result.m_vHitPoint.z};
        output.plane_normal = {result.m_vHitNormal.x, result.m_vHitNormal.y,
                               result.m_vHitNormal.z};
        output.contents = static_cast<int>(result.m_nContents);
        if (result.m_pEnt && result.m_pEnt->m_pEntity) {
            output.entity_index = result.m_pEnt->GetEntityIndex().Get();
            output.entity_handle = result.m_pEnt->GetRefEHandle().ToInt();
        }
        if (result.m_pHitbox) {
            output.hitbox = result.m_pHitbox->m_nHitBoxIndex;
            output.hitgroup = result.m_pHitbox->m_nGroupId;
        }
        if (result.m_pSurfaceProperties) {
            const char* name = result.m_pSurfaceProperties->m_name.String();
            copy_text(output.surface_name, sizeof(output.surface_name),
                      name ? name : "");
        }
        return true;
    }

    static GrenadeType grenade_type(std::string_view classname) {
        if (classname.find("hegrenade") != std::string_view::npos)
            return GrenadeType::HighExplosive;
        if (classname.find("flashbang") != std::string_view::npos)
            return GrenadeType::Flashbang;
        if (classname.find("smokegrenade") != std::string_view::npos)
            return GrenadeType::Smoke;
        if (classname.find("incendiary") != std::string_view::npos ||
            classname.find("incgrenade") != std::string_view::npos)
            return GrenadeType::Incendiary;
        if (classname.find("molotov") != std::string_view::npos)
            return GrenadeType::Molotov;
        if (classname.find("decoy") != std::string_view::npos)
            return GrenadeType::Decoy;
        return GrenadeType::Unknown;
    }

    std::vector<int> grenades(int type, std::string& error) {
        std::vector<int> output;
        if (!ready(error)) return output;
        auto* system = entity_system();
        for (CEntityIdentity* identity =
                 system->m_EntityList.m_pFirstActiveEntity;
             identity; identity = identity->m_pNext) {
            if (!identity->m_pInstance) continue;
            const char* raw = identity->GetClassname();
            const GrenadeType kind = grenade_type(raw ? raw : "");
            if (kind == GrenadeType::Unknown) continue;
            if (type != static_cast<int>(GrenadeType::Unknown) &&
                type != static_cast<int>(kind)) {
                continue;
            }
            output.push_back(identity->GetEntityIndex().Get());
        }
        return output;
    }

    bool optional_value(int entity_index, std::string_view path,
                        PropertyValue& value) {
        std::string ignored;
        return property_get(entity_index, path, -1, value, ignored);
    }

    bool grenade_get(int entity_index, GrenadeInfo& output,
                     std::string& error) {
        if (!ready(error)) return false;
        CEntityInstance* instance = entity(entity_index);
        if (!instance || !instance->m_pEntity) {
            error = "grenade entity index is invalid";
            return false;
        }
        const char* raw = instance->GetClassname();
        const GrenadeType kind = grenade_type(raw ? raw : "");
        if (kind == GrenadeType::Unknown) {
            error = "entity is not a supported grenade projectile";
            return false;
        }
        const auto* world = LuaCS_GetWorldServices();
        EntityInfo entity_info;
        char world_error[256]{};
        if (!world || !world->entity_get ||
            !world->entity_get(world->context, entity_index, &entity_info,
                               world_error, sizeof(world_error))) {
            error = world_error[0] ? world_error : "world entity service failed";
            return false;
        }
        output = {};
        output.valid = true;
        output.type = kind;
        output.entity_index = entity_index;
        output.handle = entity_info.handle;
        output.owner_entity_index = entity_info.owner_index;
        output.position = entity_info.position;
        output.velocity = entity_info.velocity;
        copy_text(output.classname, sizeof(output.classname),
                  entity_info.classname);

        PropertyValue value;
        if (optional_value(entity_index, "m_flSpawnTime", value))
            output.spawn_time = static_cast<float>(value.float_value);
        if (optional_value(entity_index, "m_flDetonateTime", value))
            output.detonate_time = static_cast<float>(value.float_value);
        if (optional_value(entity_index, "m_bHasExploded", value) ||
            optional_value(entity_index, "m_bExploded", value))
            output.exploded = value.boolean_value;
        if (optional_value(entity_index, "m_bDidSmokeEffect", value) ||
            optional_value(entity_index, "m_bSmokeEffectSpawned", value))
            output.smoke_active = value.boolean_value;
        if (optional_value(entity_index, "m_hThrower", value) &&
            value.entity_index >= 0) {
            // Player pawn indexes map back to controller slots through the
            // currently active player-controller entities.
            for (int slot = 0; slot < 64; ++slot) {
                CEntityInstance* controller = entity(slot + 1);
                if (!controller) continue;
                PropertyValue pawn;
                if (optional_value(slot + 1, "m_hPlayerPawn", pawn) &&
                    pawn.entity_index == value.entity_index) {
                    output.thrower_slot = slot;
                    break;
                }
            }
        }
        return true;
    }

    const char* grenade_class(GrenadeType type) const {
        switch (type) {
            case GrenadeType::HighExplosive: return "hegrenade_projectile";
            case GrenadeType::Flashbang: return "flashbang_projectile";
            case GrenadeType::Smoke: return "smokegrenade_projectile";
            case GrenadeType::Molotov: return "molotov_projectile";
            case GrenadeType::Incendiary: return "incendiarygrenade_projectile";
            case GrenadeType::Decoy: return "decoy_projectile";
            default: return nullptr;
        }
    }

    bool grenade_spawn(const GrenadeSpawnRequest& request,
                       GrenadeInfo& output, std::string& error) {
        if (!ready(error)) return false;
        const char* classname = grenade_class(request.type);
        if (!classname) {
            error = "unsupported grenade type";
            return false;
        }
        const auto* world = LuaCS_GetWorldServices();
        if (!world || !world->entity_create || !world->entity_spawn ||
            !world->entity_teleport) {
            error = "world entity creation service is unavailable";
            return false;
        }
        EntityInfo created;
        char world_error[256]{};
        if (!world->entity_create(world->context, classname, &created,
                                  world_error, sizeof(world_error))) {
            error = world_error;
            return false;
        }
        if (!world->entity_teleport(world->context, created.entity_index,
                                    &request.position, &request.angles,
                                    &request.velocity, world_error,
                                    sizeof(world_error))) {
            if (world->entity_remove)
                world->entity_remove(world->context, created.entity_index,
                                     world_error, sizeof(world_error));
            error = world_error;
            return false;
        }
        if (request.owner_entity_index >= 0 && world->entity_set_owner &&
            !world->entity_set_owner(world->context, created.entity_index,
                                     request.owner_entity_index, world_error,
                                     sizeof(world_error))) {
            error = world_error;
            return false;
        }
        if (request.thrower_slot >= 0) {
            PropertyValue pawn;
            if (!optional_value(request.thrower_slot + 1, "m_hPlayerPawn", pawn)) {
                error = "thrower slot has no live player pawn";
                return false;
            }
            PropertyValue handle;
            handle.kind = PropertyKind::EntityHandle;
            handle.entity_index = pawn.entity_index;
            if (!property_set(created.entity_index, "m_hThrower", -1, handle,
                              true, error)) {
                return false;
            }
        }
        if (request.spawn &&
            !world->entity_spawn(world->context, created.entity_index,
                                 world_error, sizeof(world_error))) {
            error = world_error;
            return false;
        }
        if (request.fuse_seconds >= 0.0f && world->entity_accept_input &&
            !world->entity_accept_input(
                world->context, created.entity_index, "Detonate", "",
                request.owner_entity_index, request.owner_entity_index,
                request.fuse_seconds, world_error, sizeof(world_error))) {
            error = world_error;
            return false;
        }
        return grenade_get(created.entity_index, output, error);
    }

    bool grenade_detonate(int entity_index, std::string& error) {
        GrenadeInfo info;
        if (!grenade_get(entity_index, info, error)) return false;
        const auto* world = LuaCS_GetWorldServices();
        char world_error[256]{};
        if (world && world->entity_accept_input &&
            world->entity_accept_input(world->context, entity_index,
                                       "Detonate", "", entity_index,
                                       entity_index, 0.0f, world_error,
                                       sizeof(world_error))) {
            return true;
        }
        error = world_error[0] ? world_error : "grenade rejected Detonate input";
        return false;
    }

    bool grenade_remove(int entity_index, std::string& error) {
        GrenadeInfo info;
        if (!grenade_get(entity_index, info, error)) return false;
        const auto* world = LuaCS_GetWorldServices();
        char world_error[256]{};
        if (!world || !world->entity_remove ||
            !world->entity_remove(world->context, entity_index, world_error,
                                  sizeof(world_error))) {
            error = world_error[0] ? world_error : "entity removal service failed";
            return false;
        }
        return true;
    }

    AdvancedWorldServices services{};

private:
    bool fail_init(std::string message, std::string& error) {
        initialization_error_ = std::move(message);
        error = initialization_error_;
        return false;
    }

    static LuaCSAdvancedApi* self(void* context) {
        return static_cast<LuaCSAdvancedApi*>(context);
    }

    static bool property_info_bridge(void* context, int entity_index,
                                     const char* property, PropertyInfo* output,
                                     char* error, std::size_t error_size) {
        if (error && error_size) error[0] = '\0';
        std::string message;
        const bool result = property && output && self(context)->property_info(
            entity_index, property, *output, message);
        if (!result) copy_text(error, error_size, message);
        return result;
    }

    static bool property_get_bridge(void* context, int entity_index,
                                    const char* property, int array_index,
                                    PropertyValue* output, char* error,
                                    std::size_t error_size) {
        if (error && error_size) error[0] = '\0';
        std::string message;
        const bool result = property && output && self(context)->property_get(
            entity_index, property, array_index, *output, message);
        if (!result) copy_text(error, error_size, message);
        return result;
    }

    static bool property_set_bridge(void* context, int entity_index,
                                    const char* property, int array_index,
                                    const PropertyValue* value, bool network,
                                    char* error, std::size_t error_size) {
        if (error && error_size) error[0] = '\0';
        std::string message;
        const bool result = property && value && self(context)->property_set(
            entity_index, property, array_index, *value, network, message);
        if (!result) copy_text(error, error_size, message);
        return result;
    }

    static std::size_t property_count_bridge(void* context, int entity_index,
                                             bool inherited, char* error,
                                             std::size_t error_size) {
        if (error && error_size) error[0] = '\0';
        std::string message;
        auto values = self(context)->property_list(entity_index, inherited,
                                                   message);
        if (!message.empty()) copy_text(error, error_size, message);
        return values.size();
    }

    static bool property_at_bridge(void* context, int entity_index,
                                   bool inherited, std::size_t index,
                                   PropertyInfo* output, char* error,
                                   std::size_t error_size) {
        if (error && error_size) error[0] = '\0';
        std::string message;
        auto values = self(context)->property_list(entity_index, inherited,
                                                   message);
        if (index >= values.size()) {
            message = "property index is out of range";
            copy_text(error, error_size, message);
            return false;
        }
        *output = values[index];
        return true;
    }

    static bool trace_bridge(void* context, const TraceRequest* request,
                             TraceResult* output, char* error,
                             std::size_t error_size) {
        if (error && error_size) error[0] = '\0';
        std::string message;
        const bool result = request && output &&
                            self(context)->trace(*request, *output, message);
        if (!result) copy_text(error, error_size, message);
        return result;
    }

    static bool grenade_get_bridge(void* context, int entity_index,
                                   GrenadeInfo* output, char* error,
                                   std::size_t error_size) {
        if (error && error_size) error[0] = '\0';
        std::string message;
        const bool result = output && self(context)->grenade_get(
            entity_index, *output, message);
        if (!result) copy_text(error, error_size, message);
        return result;
    }

    static std::size_t grenade_count_bridge(void* context, int type,
                                            char* error,
                                            std::size_t error_size) {
        if (error && error_size) error[0] = '\0';
        std::string message;
        auto values = self(context)->grenades(type, message);
        if (!message.empty()) copy_text(error, error_size, message);
        return values.size();
    }

    static bool grenade_at_bridge(void* context, int type, std::size_t index,
                                  GrenadeInfo* output, char* error,
                                  std::size_t error_size) {
        if (error && error_size) error[0] = '\0';
        std::string message;
        auto* api = self(context);
        auto values = api->grenades(type, message);
        if (index >= values.size()) {
            message = "grenade result index is out of range";
            copy_text(error, error_size, message);
            return false;
        }
        const bool result = output &&
                            api->grenade_get(values[index], *output, message);
        if (!result) copy_text(error, error_size, message);
        return result;
    }

    static bool grenade_spawn_bridge(void* context,
                                     const GrenadeSpawnRequest* request,
                                     GrenadeInfo* output, char* error,
                                     std::size_t error_size) {
        if (error && error_size) error[0] = '\0';
        std::string message;
        const bool result = request && output && self(context)->grenade_spawn(
            *request, *output, message);
        if (!result) copy_text(error, error_size, message);
        return result;
    }

    static bool grenade_detonate_bridge(void* context, int entity_index,
                                        char* error,
                                        std::size_t error_size) {
        if (error && error_size) error[0] = '\0';
        std::string message;
        const bool result =
            self(context)->grenade_detonate(entity_index, message);
        if (!result) copy_text(error, error_size, message);
        return result;
    }

    static bool grenade_remove_bridge(void* context, int entity_index,
                                      char* error,
                                      std::size_t error_size) {
        if (error && error_size) error[0] = '\0';
        std::string message;
        const bool result = self(context)->grenade_remove(entity_index, message);
        if (!result) copy_text(error, error_size, message);
        return result;
    }

    bool initialized_{};
    bool attempted_{};
    std::string initialization_error_;
    void* game_resource_service_{};
    std::size_t entity_system_offset_{};
    mutable CGameEntitySystem* entity_system_{};
    CSchemaSystem* schema_system_{};
    CSchemaSystemTypeScope* scope_{};
    void* trace_manager_{};
    TraceSimpleFn trace_simple_{};
    TraceShapeFn trace_shape_{};
};

LuaCSAdvancedApi g_advanced_api;

} // namespace

extern "C" __declspec(dllexport) const luacs::AdvancedWorldServices*
LuaCS_GetAdvancedWorldServices() {
    return &g_advanced_api.services;
}
