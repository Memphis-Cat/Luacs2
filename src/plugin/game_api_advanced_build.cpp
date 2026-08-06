#include "game_api_internal.h"
#include "luacs/advanced_world_api.h"
#include "luacs/world_api.h"

#include <gametrace.h>
#include <ray.h>
#include <schemasystem/schemasystem.h>
#include <schemasystem/schematypes.h>
#include <tier1/utlstring.h>
#include <tier1/utlsymbollarge.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <climits>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <limits>
#include <optional>
#include <regex>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

// PropertyInfo stores a cross-DLL 16-bit element size while builtin schema
// widths are represented by an 8-bit value. This overload performs the checked
// conversion and preserves outer fixed-array/collection metadata.
luacs::PropertyKind classify(CSchemaType* type, std::uint16_t& width,
                             std::uint16_t& array_count,
                             std::uint16_t& element_size);

} // namespace

// Keep the schema implementation in one translation unit. The complete wrapper
// that includes this file supplies the final trace and grenade callbacks.
#define private public
#include "game_api_advanced.cpp"
#undef private

namespace {

using luacs::PropertyInfo;
using luacs::PropertyKind;
using luacs::RawPropertyValue;

luacs::PropertyKind classify(CSchemaType* type, std::uint16_t& width,
                             std::uint16_t& array_count,
                             std::uint16_t& element_size) {
    width = 0;
    array_count = 0;
    element_size = 0;
    if (!type) return PropertyKind::Invalid;

    const bool width_aliases_element_size = &width == &element_size;

    if (type->m_eTypeCategory == SCHEMA_TYPE_FIXED_ARRAY) {
        const auto* array = static_cast<CSchemaType_FixedArray*>(type);
        array_count = static_cast<std::uint16_t>(
            std::clamp(array->m_nElementCount, 0, 65535));
        const std::uint16_t native_element_size =
            static_cast<std::uint16_t>(
                std::clamp(static_cast<int>(array->m_nElementSize), 0, 65535));

        std::uint8_t native_width{};
        std::uint16_t ignored_count{};
        std::uint16_t ignored_element_size{};
        PropertyKind kind = classify(array->m_pElementType, native_width,
                                     ignored_count, ignored_element_size);
        if (array->m_pElementType &&
            array->m_pElementType->m_eTypeCategory == SCHEMA_TYPE_BUILTIN &&
            static_cast<CSchemaType_Builtin*>(array->m_pElementType)
                    ->m_eBuiltinType == SCHEMA_BUILTIN_TYPE_CHAR) {
            kind = PropertyKind::String;
        }

        if (width_aliases_element_size) {
            width = native_element_size ? native_element_size : native_width;
        } else {
            width = native_width;
            element_size = native_element_size ? native_element_size
                                               : native_width;
        }
        return kind;
    }

    if (type->m_eTypeCategory == SCHEMA_TYPE_ATOMIC &&
        type->m_eAtomicCategory == SCHEMA_ATOMIC_COLLECTION_OF_T) {
        const auto* collection =
            static_cast<CSchemaType_Atomic_CollectionOfT*>(type);
        const std::uint16_t native_element_size = collection->m_nElementSize;

        std::uint8_t native_width{};
        std::uint16_t ignored_count{};
        std::uint16_t ignored_element_size{};
        const PropertyKind kind =
            classify(collection->m_pTemplateType, native_width, ignored_count,
                     ignored_element_size);
        if (width_aliases_element_size) {
            width = native_element_size ? native_element_size : native_width;
        } else {
            width = native_width;
            element_size = native_element_size ? native_element_size
                                               : native_width;
        }
        return kind;
    }

    std::uint8_t native_width{};
    std::uint16_t native_array_count{};
    std::uint16_t native_element_size{};
    const PropertyKind kind =
        classify(type, native_width, native_array_count, native_element_size);
    array_count = native_array_count;
    if (width_aliases_element_size) {
        width = native_element_size ? native_element_size : native_width;
    } else {
        width = native_width;
        element_size = native_element_size ? native_element_size : native_width;
    }
    return kind;
}

void write_error(char* destination, std::size_t capacity,
                 std::string_view message) {
    copy_text(destination, capacity, message);
}

bool finite_vector(const luacs::Vector3& value) {
    return std::isfinite(value.x) && std::isfinite(value.y) &&
           std::isfinite(value.z);
}

bool is_collection(CSchemaType* type) {
    return type && type->m_eTypeCategory == SCHEMA_TYPE_ATOMIC &&
           type->m_eAtomicCategory == SCHEMA_ATOMIC_COLLECTION_OF_T;
}

bool schema_size(CSchemaType* type, std::size_t& output) {
    output = 0;
    if (!type) return false;

    int size{};
    std::uint8_t alignment{};
    if (type->GetSizeAndAlignment(size, alignment) && size > 0) {
        output = static_cast<std::size_t>(size);
        return true;
    }

    switch (type->m_eTypeCategory) {
        case SCHEMA_TYPE_BUILTIN:
            output = static_cast<CSchemaType_Builtin*>(type)->m_nSize;
            return output != 0;
        case SCHEMA_TYPE_POINTER:
            output = sizeof(void*);
            return true;
        case SCHEMA_TYPE_BITFIELD: {
            const int bits = static_cast<CSchemaType_Bitfield*>(type)
                                 ->m_nBitfieldCount;
            output = static_cast<std::size_t>(std::clamp((bits + 7) / 8, 1, 8));
            return true;
        }
        case SCHEMA_TYPE_FIXED_ARRAY: {
            const auto* array = static_cast<CSchemaType_FixedArray*>(type);
            if (array->m_nElementCount <= 0 || array->m_nElementSize == 0)
                return false;
            const std::size_t count =
                static_cast<std::size_t>(array->m_nElementCount);
            const std::size_t element = array->m_nElementSize;
            if (count > std::numeric_limits<std::size_t>::max() / element)
                return false;
            output = count * element;
            return true;
        }
        case SCHEMA_TYPE_ATOMIC:
            output = static_cast<CSchemaType_Atomic*>(type)->m_nSize;
            return output != 0;
        case SCHEMA_TYPE_DECLARED_CLASS: {
            const auto* declared = static_cast<CSchemaType_DeclaredClass*>(type);
            if (!declared->m_pClassInfo || declared->m_pClassInfo->m_nSize <= 0)
                return false;
            output = static_cast<std::size_t>(declared->m_pClassInfo->m_nSize);
            return true;
        }
        case SCHEMA_TYPE_DECLARED_ENUM: {
            const auto* declared = static_cast<CSchemaType_DeclaredEnum*>(type);
            if (!declared->m_pEnumInfo || declared->m_pEnumInfo->m_nSize == 0)
                return false;
            output = declared->m_pEnumInfo->m_nSize;
            return true;
        }
        default:
            return false;
    }
}

CSchemaClassInfo* class_from_type(CSchemaType* type) {
    if (!type) return nullptr;
    if (type->m_eTypeCategory == SCHEMA_TYPE_POINTER) {
        type = static_cast<CSchemaType_Ptr*>(type)->m_pObjectType;
    }
    if (!type || type->m_eTypeCategory != SCHEMA_TYPE_DECLARED_CLASS)
        return nullptr;
    return static_cast<CSchemaType_DeclaredClass*>(type)->m_pClassInfo;
}

void enrich_property_info(const ResolvedProperty& property,
                          PropertyInfo& output) {
    output = property.info;
    output.fixed_array =
        property.type && property.type->m_eTypeCategory == SCHEMA_TYPE_FIXED_ARRAY;
    output.collection = is_collection(property.type);
    output.pointer =
        property.type && property.type->m_eTypeCategory == SCHEMA_TYPE_POINTER;
    output.embedded_class = class_from_type(property.type) != nullptr;
    output.selected_index = property.array_index;

    std::size_t byte_size{};
    if (schema_size(property.type, byte_size) &&
        byte_size <= std::numeric_limits<std::uint32_t>::max()) {
        output.byte_size = static_cast<std::uint32_t>(byte_size);
        output.readable = !output.collection;
        if (output.kind == PropertyKind::Invalid && !output.collection) {
            output.kind = PropertyKind::Raw;
            output.writable = true;
        }
    }

    if (property.entity) {
        if (auto* owner = property.entity->Schema_DynamicBinding().Get()) {
            copy_text(output.owner_class, sizeof(output.owner_class),
                      owner->m_pszName ? owner->m_pszName : "");
        }
        const auto root = reinterpret_cast<std::uintptr_t>(property.entity);
        const auto address = reinterpret_cast<std::uintptr_t>(property.address);
        if (!output.pointer && address >= root) {
            const auto difference = address - root;
            if (difference <= std::numeric_limits<std::uint32_t>::max()) {
                output.offset = static_cast<std::uint32_t>(difference);
            }
        }
    }
}

bool resolve_property(void* context, int entity_index, const char* property,
                      int array_index, ResolvedProperty& output,
                      std::string& message) {
    if (!context || !property) {
        message = "property context or path is null";
        return false;
    }
    return static_cast<LuaCSAdvancedApi*>(context)->resolve(
        entity_index, property, array_index, output, message);
}

bool property_info_complete(void* context, int entity_index,
                            const char* property, PropertyInfo* output,
                            char* error, std::size_t error_size) {
    if (error && error_size) error[0] = '\0';
    if (!output) {
        write_error(error, error_size, "property metadata output is null");
        return false;
    }
    ResolvedProperty resolved;
    std::string message;
    if (!resolve_property(context, entity_index, property, -1, resolved,
                          message)) {
        write_error(error, error_size, message);
        return false;
    }
    enrich_property_info(resolved, *output);
    return true;
}

bool property_at_complete(void* context, int entity_index, bool inherited,
                          std::size_t index, PropertyInfo* output, char* error,
                          std::size_t error_size) {
    if (error && error_size) error[0] = '\0';
    if (!context || !output) {
        write_error(error, error_size, "property enumeration output is null");
        return false;
    }
    auto* api = static_cast<LuaCSAdvancedApi*>(context);
    std::string message;
    const auto values = api->property_list(entity_index, inherited, message);
    if (!message.empty()) {
        write_error(error, error_size, message);
        return false;
    }
    if (index >= values.size()) {
        write_error(error, error_size, "property index is out of range");
        return false;
    }
    if (!property_info_complete(context, entity_index, values[index].name,
                                output, error, error_size)) {
        *output = values[index];
        return false;
    }
    return true;
}

bool property_get_raw_complete(void* context, int entity_index,
                               const char* property, int array_index,
                               RawPropertyValue* output, PropertyInfo* info,
                               char* error, std::size_t error_size) {
    if (error && error_size) error[0] = '\0';
    if (!output) {
        write_error(error, error_size, "raw property output is null");
        return false;
    }
    ResolvedProperty resolved;
    std::string message;
    if (!resolve_property(context, entity_index, property, array_index,
                          resolved, message)) {
        write_error(error, error_size, message);
        return false;
    }
    if (is_collection(resolved.type)) {
        write_error(error, error_size,
                    "dynamic collection raw reads require an element index");
        return false;
    }
    std::size_t size{};
    if (!schema_size(resolved.type, size) || size == 0) {
        write_error(error, error_size,
                    "schema type does not expose a stable byte size");
        return false;
    }
    if (size > luacs::kPropertyRawCapacity) {
        write_error(error, error_size,
                    "schema value exceeds the bounded raw buffer capacity");
        return false;
    }
    output->size = size;
    std::memcpy(output->bytes, resolved.address, size);
    if (info) enrich_property_info(resolved, *info);
    return true;
}

bool property_set_raw_complete(void* context, int entity_index,
                               const char* property, int array_index,
                               const RawPropertyValue* input, bool network,
                               char* error, std::size_t error_size) {
    if (error && error_size) error[0] = '\0';
    if (!input) {
        write_error(error, error_size, "raw property input is null");
        return false;
    }
    ResolvedProperty resolved;
    std::string message;
    if (!resolve_property(context, entity_index, property, array_index,
                          resolved, message)) {
        write_error(error, error_size, message);
        return false;
    }
    if (is_collection(resolved.type)) {
        write_error(error, error_size,
                    "dynamic collection raw writes require an element index");
        return false;
    }
    std::size_t size{};
    if (!schema_size(resolved.type, size) || size == 0) {
        write_error(error, error_size,
                    "schema type does not expose a stable byte size");
        return false;
    }
    if (size > luacs::kPropertyRawCapacity || input->size != size) {
        write_error(error, error_size,
                    "raw property byte count does not match the schema size");
        return false;
    }
    std::memcpy(resolved.address, input->bytes, size);
    if (network) {
        resolved.entity->NetworkStateChanged(NetworkStateChangedData(
            resolved.root_offset, resolved.array_index));
    }
    return true;
}

std::size_t property_collection_count_complete(
    void* context, int entity_index, const char* property, char* error,
    std::size_t error_size) {
    if (error && error_size) error[0] = '\0';
    ResolvedProperty resolved;
    std::string message;
    if (!resolve_property(context, entity_index, property, -1, resolved,
                          message)) {
        write_error(error, error_size, message);
        return 0;
    }
    if (!is_collection(resolved.type)) {
        write_error(error, error_size,
                    "schema property is not a dynamic collection");
        return 0;
    }
    const auto* collection =
        static_cast<CSchemaType_Atomic_CollectionOfT*>(resolved.type);
    if (!collection->m_pfnManipulator) {
        write_error(error, error_size,
                    "schema collection has no manipulator");
        return 0;
    }
    return reinterpret_cast<std::uintptr_t>(collection->m_pfnManipulator(
        SCHEMA_COLLECTION_MANIPULATOR_ACTION_GET_COUNT, resolved.address, 0,
        0));
}

bool property_collection_resize_complete(
    void* context, int entity_index, const char* property, std::size_t count,
    bool network, char* error, std::size_t error_size) {
    if (error && error_size) error[0] = '\0';
    if (count > static_cast<std::size_t>(INT_MAX)) {
        write_error(error, error_size,
                    "collection size exceeds the engine index range");
        return false;
    }
    ResolvedProperty resolved;
    std::string message;
    if (!resolve_property(context, entity_index, property, -1, resolved,
                          message)) {
        write_error(error, error_size, message);
        return false;
    }
    if (!is_collection(resolved.type)) {
        write_error(error, error_size,
                    "schema property is not a dynamic collection");
        return false;
    }
    const auto* collection =
        static_cast<CSchemaType_Atomic_CollectionOfT*>(resolved.type);
    if (!collection->m_pfnManipulator) {
        write_error(error, error_size,
                    "schema collection has no manipulator");
        return false;
    }
    collection->m_pfnManipulator(SCHEMA_COLLECTION_MANIPULATOR_ACTION_SET_COUNT,
                                 resolved.address, static_cast<int>(count), 0);
    if (network) {
        resolved.entity->NetworkStateChanged(
            NetworkStateChangedData(resolved.root_offset, -1));
    }
    return true;
}

bool resolve_child_class(void* context, int entity_index, const char* property,
                         CSchemaClassInfo*& class_info,
                         std::string& message) {
    auto* api = static_cast<LuaCSAdvancedApi*>(context);
    if (!api) {
        message = "advanced property context is null";
        return false;
    }
    if (!property || !*property) {
        if (!api->ready(message)) return false;
        CEntityInstance* instance = api->entity(entity_index);
        if (!instance) {
            message = "entity index is invalid";
            return false;
        }
        class_info = instance->Schema_DynamicBinding().Get();
        if (!class_info) message = "entity has no dynamic schema binding";
        return class_info != nullptr;
    }

    ResolvedProperty resolved;
    if (!api->resolve(entity_index, property, -1, resolved, message))
        return false;
    class_info = class_from_type(resolved.type);
    if (!class_info)
        message = "schema property is not an embedded or pointed-to class";
    return class_info != nullptr;
}

std::size_t property_child_count_complete(void* context, int entity_index,
                                          const char* property,
                                          bool inherited, char* error,
                                          std::size_t error_size) {
    if (error && error_size) error[0] = '\0';
    CSchemaClassInfo* class_info{};
    std::string message;
    if (!resolve_child_class(context, entity_index, property, class_info,
                             message)) {
        write_error(error, error_size, message);
        return 0;
    }
    std::vector<PropertyInfo> values;
    static_cast<LuaCSAdvancedApi*>(context)->enumerate_class(
        class_info, inherited, values);
    return values.size();
}

bool property_child_at_complete(void* context, int entity_index,
                                const char* property, bool inherited,
                                std::size_t index, PropertyInfo* output,
                                char* error, std::size_t error_size) {
    if (error && error_size) error[0] = '\0';
    if (!output) {
        write_error(error, error_size, "child property output is null");
        return false;
    }
    CSchemaClassInfo* class_info{};
    std::string message;
    if (!resolve_child_class(context, entity_index, property, class_info,
                             message)) {
        write_error(error, error_size, message);
        return false;
    }
    std::vector<PropertyInfo> values;
    static_cast<LuaCSAdvancedApi*>(context)->enumerate_class(
        class_info, inherited, values);
    if (index >= values.size()) {
        write_error(error, error_size, "child property index is out of range");
        return false;
    }
    std::string path;
    if (property && *property) {
        path.assign(property);
        path.push_back('.');
    }
    path.append(values[index].name);
    return property_info_complete(context, entity_index, path.c_str(), output,
                                  error, error_size);
}

struct AdvancedWorldPropertyRegistration {
    AdvancedWorldPropertyRegistration() {
        auto& services = g_advanced_api.services;
        services.property_info = &property_info_complete;
        services.property_at = &property_at_complete;
        services.property_get_raw = &property_get_raw_complete;
        services.property_set_raw = &property_set_raw_complete;
        services.property_collection_count =
            &property_collection_count_complete;
        services.property_collection_resize =
            &property_collection_resize_complete;
        services.property_child_count = &property_child_count_complete;
        services.property_child_at = &property_child_at_complete;
    }
};

AdvancedWorldPropertyRegistration g_advanced_world_property_registration;

} // namespace
