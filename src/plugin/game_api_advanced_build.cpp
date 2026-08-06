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
// conversion and leaves the core classifier's exact engine types unchanged.
luacs::PropertyKind classify(CSchemaType* type, std::uint16_t& width,
                             std::uint16_t& array_count,
                             std::uint16_t& element_size);

} // namespace

// The adapter is the compatibility boundary around the implementation file. It
// needs access to the resolved native trace entry points so ABI v2 can use the
// real Source 2 CTraceFilter rather than dropping its new filter fields.
#define private public
#include "game_api_advanced.cpp"
#undef private

namespace {

using luacs::PropertyInfo;
using luacs::PropertyKind;
using luacs::RawPropertyValue;
using luacs::TraceRequest;
using luacs::TraceResult;
using luacs::TraceShape;

luacs::PropertyKind classify(CSchemaType* type, std::uint16_t& width,
                             std::uint16_t& array_count,
                             std::uint16_t& element_size) {
    width = 0;
    array_count = 0;
    element_size = 0;
    if (!type) return PropertyKind::Invalid;

    const bool width_aliases_element_size = &width == &element_size;

    // The core 8-bit classifier recursively descends into arrays and
    // collections. Preserve their outer metadata here instead of allowing the
    // recursive call to reset array_count/element_size to zero.
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

bool resolve_v2(void* context, int entity_index, const char* property,
                int array_index, ResolvedProperty& output,
                std::string& message) {
    if (!context || !property) {
        message = "property context or path is null";
        return false;
    }
    return static_cast<LuaCSAdvancedApi*>(context)->resolve(
        entity_index, property, array_index, output, message);
}

bool property_info_v2(void* context, int entity_index, const char* property,
                      PropertyInfo* output, char* error,
                      std::size_t error_size) {
    if (error && error_size) error[0] = '\0';
    if (!output) {
        write_error(error, error_size, "property metadata output is null");
        return false;
    }
    ResolvedProperty resolved;
    std::string message;
    if (!resolve_v2(context, entity_index, property, -1, resolved, message)) {
        write_error(error, error_size, message);
        return false;
    }
    enrich_property_info(resolved, *output);
    return true;
}

bool property_at_v2(void* context, int entity_index, bool inherited,
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
    if (!property_info_v2(context, entity_index, values[index].name, output,
                          error, error_size)) {
        *output = values[index];
        return false;
    }
    return true;
}

bool property_get_raw_v2(void* context, int entity_index,
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
    if (!resolve_v2(context, entity_index, property, array_index, resolved,
                    message)) {
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

bool property_set_raw_v2(void* context, int entity_index,
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
    if (!resolve_v2(context, entity_index, property, array_index, resolved,
                    message)) {
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

std::size_t property_collection_count_v2(void* context, int entity_index,
                                         const char* property, char* error,
                                         std::size_t error_size) {
    if (error && error_size) error[0] = '\0';
    ResolvedProperty resolved;
    std::string message;
    if (!resolve_v2(context, entity_index, property, -1, resolved, message)) {
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

bool property_collection_resize_v2(void* context, int entity_index,
                                   const char* property, std::size_t count,
                                   bool network, char* error,
                                   std::size_t error_size) {
    if (error && error_size) error[0] = '\0';
    if (count > static_cast<std::size_t>(INT_MAX)) {
        write_error(error, error_size,
                    "collection size exceeds the engine index range");
        return false;
    }
    ResolvedProperty resolved;
    std::string message;
    if (!resolve_v2(context, entity_index, property, -1, resolved, message)) {
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

std::size_t property_child_count_v2(void* context, int entity_index,
                                    const char* property, bool inherited,
                                    char* error, std::size_t error_size) {
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

bool property_child_at_v2(void* context, int entity_index,
                          const char* property, bool inherited,
                          std::size_t index, PropertyInfo* output, char* error,
                          std::size_t error_size) {
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
    return property_info_v2(context, entity_index, path.c_str(), output, error,
                            error_size);
}

bool finite_vector(const luacs::Vector3& value) {
    return std::isfinite(value.x) && std::isfinite(value.y) &&
           std::isfinite(value.z);
}

bool append_ignore(int value, int* output, std::size_t& count) {
    if (value < 0) return true;
    for (std::size_t index = 0; index < count; ++index) {
        if (output[index] == value) return true;
    }
    if (count >= 2) return false;
    output[count++] = value;
    return true;
}

bool trace_v2(void* context, const TraceRequest* request, TraceResult* output,
              char* error, std::size_t error_size) {
    if (error && error_size) error[0] = '\0';
    if (!context || !request || !output) {
        write_error(error, error_size, "trace context, request, or output is null");
        return false;
    }
    if (!finite_vector(request->start) || !finite_vector(request->end)) {
        write_error(error, error_size, "trace coordinates must be finite");
        return false;
    }
    if (request->ignore_count > luacs::kTraceIgnoreCapacity) {
        write_error(error, error_size, "trace ignore count exceeds ABI capacity");
        return false;
    }

    const TraceShape shape =
        request->use_hull ? TraceShape::Hull : request->shape;
    if (shape == TraceShape::Sphere || shape == TraceShape::Capsule) {
        write_error(error, error_size,
                    "sphere and capsule traces require a verified native Ray_t layout and are not enabled yet");
        return false;
    }

    int ignored[2]{-1, -1};
    std::size_t ignored_count{};
    if (!append_ignore(request->ignore_entity_index, ignored, ignored_count)) {
        write_error(error, error_size,
                    "the current Source 2 CTraceFilter supports two ignored entities");
        return false;
    }
    for (std::size_t index = 0; index < request->ignore_count; ++index) {
        if (!append_ignore(request->ignore_entities[index], ignored,
                           ignored_count)) {
            write_error(error, error_size,
                        "the current Source 2 CTraceFilter supports two ignored entities");
            return false;
        }
    }

    auto* api = static_cast<LuaCSAdvancedApi*>(context);
    std::string message;
    if (!api->ready(message)) {
        write_error(error, error_size, message);
        return false;
    }

    const std::uint64_t interacts_with =
        request->interacts_with
            ? request->interacts_with
            : (request->contents ? request->contents : request->contents_mask);
    const int collision_group = static_cast<int>(request->collision_group);
    const bool iterate_entities = request->iterate_entities && request->hit_entities;

    CTraceFilter filter(interacts_with, collision_group, iterate_entities);
    filter.m_nInteractsAs = request->interacts_as;
    filter.m_nInteractsWith = interacts_with;
    filter.m_nInteractsExclude = request->interacts_exclude;
    filter.m_nObjectSetMask = static_cast<std::uint8_t>(
        request->ignore_entities_mask ? request->ignore_entities_mask : 0x0F);
    filter.m_nCollisionGroup = collision_group;
    filter.m_bHitSolid = request->hit_solid;
    filter.m_bHitTrigger = request->hit_triggers;
    filter.m_bShouldIgnoreDisabledPairs = request->ignore_disabled_pairs;
    filter.m_bIgnoreIfBothInteractWithHitboxes =
        request->ignore_if_both_hitboxes;
    filter.m_bForceHitEverything = request->force_hit_everything;
    filter.m_bIterateEntities = iterate_entities;
    if (ignored_count > 0) filter.SetPassEntity1(api->entity(ignored[0]));
    if (ignored_count > 1) filter.SetPassEntity2(api->entity(ignored[1]));

    LuaCSAdvancedApi::NativeRay ray;
    if (shape == TraceShape::Hull) {
        if (!finite_vector(request->mins) || !finite_vector(request->maxs)) {
            write_error(error, error_size, "hull bounds must be finite");
            return false;
        }
        auto* bounds = reinterpret_cast<Vector*>(ray.data.data());
        bounds[0] = Vector(request->mins.x, request->mins.y, request->mins.z);
        bounds[1] = Vector(request->maxs.x, request->maxs.y, request->maxs.z);
        ray.type = 2;
    } else {
        ray.type = 0;
    }

    const Vector start(request->start.x, request->start.y, request->start.z);
    const Vector end(request->end.x, request->end.y, request->end.z);
    CGameTrace native;
    if (!api->trace_shape_(api->trace_manager_, &ray, &start, &end, &filter,
                           &native)) {
        write_error(error, error_size,
                    "Source 2 TraceShape rejected the request");
        return false;
    }

    *output = {};
    output->valid = true;
    output->hit = native.DidHit();
    output->start_solid = native.m_bStartInSolid;
    output->all_solid =
        native.m_bStartInSolid && native.m_flFraction <= 0.0f;
    output->exact_hit_point = native.m_bExactHitPoint;
    output->fraction = native.m_flFraction;
    output->hit_offset = native.m_flHitOffset;
    output->shape = shape;
    output->start = {native.m_vStartPos.x, native.m_vStartPos.y,
                     native.m_vStartPos.z};
    output->end = {native.m_vEndPos.x, native.m_vEndPos.y,
                   native.m_vEndPos.z};
    output->hit_position = {native.m_vHitPoint.x, native.m_vHitPoint.y,
                            native.m_vHitPoint.z};
    output->plane_normal = {native.m_vHitNormal.x, native.m_vHitNormal.y,
                            native.m_vHitNormal.z};
    output->contents = static_cast<int>(native.m_nContents);
    output->triangle = native.m_nTriangle;
    output->bone = native.m_nHitboxBoneIndex;

    const float delta_x = output->hit_position.x - output->start.x;
    const float delta_y = output->hit_position.y - output->start.y;
    const float delta_z = output->hit_position.z - output->start.z;
    output->distance =
        std::sqrt(delta_x * delta_x + delta_y * delta_y + delta_z * delta_z);
    output->plane_distance =
        output->plane_normal.x * output->hit_position.x +
        output->plane_normal.y * output->hit_position.y +
        output->plane_normal.z * output->hit_position.z;

    if (native.m_pEnt && native.m_pEnt->m_pEntity) {
        output->entity_index = native.m_pEnt->GetEntityIndex().Get();
        output->entity_handle = native.m_pEnt->GetRefEHandle().ToInt();
    }
    if (native.m_pHitbox) {
        output->hitbox = native.m_pHitbox->m_nHitBoxIndex;
        output->hitgroup = native.m_pHitbox->m_nGroupId;
    }
    if (native.m_pSurfaceProperties) {
        const char* name = native.m_pSurfaceProperties->m_name.String();
        copy_text(output->surface_name, sizeof(output->surface_name),
                  name ? name : "");
    }
    return true;
}

struct AdvancedWorldV2Registration {
    AdvancedWorldV2Registration() {
        auto& services = g_advanced_api.services;
        services.property_info = &property_info_v2;
        services.property_at = &property_at_v2;
        services.property_get_raw = &property_get_raw_v2;
        services.property_set_raw = &property_set_raw_v2;
        services.property_collection_count = &property_collection_count_v2;
        services.property_collection_resize = &property_collection_resize_v2;
        services.property_child_count = &property_child_count_v2;
        services.property_child_at = &property_child_at_v2;
        services.trace = &trace_v2;
    }
};

AdvancedWorldV2Registration g_advanced_world_v2_registration;

} // namespace
