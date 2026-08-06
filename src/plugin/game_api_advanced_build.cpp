#include "luacs/advanced_world_api.h"

#include <schemasystem/schematypes.h>

#include <algorithm>
#include <climits>
#include <cstdint>
#include <utility>

namespace {

// PropertyInfo stores a cross-DLL 16-bit element size while builtin schema
// widths are represented by an 8-bit value. This overload performs the checked
// conversion and leaves the core classifier's exact engine types unchanged.
luacs::PropertyKind classify(CSchemaType* type, std::uint16_t& width,
                             std::uint16_t& array_count,
                             std::uint16_t& element_size);

} // namespace

#include "game_api_advanced.cpp"

namespace {

luacs::PropertyKind classify(CSchemaType* type, std::uint16_t& width,
                             std::uint16_t& array_count,
                             std::uint16_t& element_size) {
    width = 0;
    array_count = 0;
    element_size = 0;
    if (!type) return luacs::PropertyKind::Invalid;

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
                std::clamp(array->m_nElementSize, 0, 65535));

        std::uint8_t native_width{};
        std::uint16_t ignored_count{};
        std::uint16_t ignored_element_size{};
        luacs::PropertyKind kind = classify(array->m_pElementType, native_width,
                                            ignored_count,
                                            ignored_element_size);
        if (array->m_pElementType &&
            array->m_pElementType->m_eTypeCategory == SCHEMA_TYPE_BUILTIN &&
            static_cast<CSchemaType_Builtin*>(array->m_pElementType)
                    ->m_eBuiltinType == SCHEMA_BUILTIN_TYPE_CHAR) {
            kind = luacs::PropertyKind::String;
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
        const std::uint16_t native_element_size =
            static_cast<std::uint16_t>(
                std::clamp(collection->m_nElementSize, 0, 65535));

        std::uint8_t native_width{};
        std::uint16_t ignored_count{};
        std::uint16_t ignored_element_size{};
        const luacs::PropertyKind kind =
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
    const luacs::PropertyKind kind =
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

} // namespace
