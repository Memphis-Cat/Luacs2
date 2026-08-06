#include "luacs/advanced_world_api.h"

#include <schemasystem/schematypes.h>

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
    std::uint8_t native_width{};
    const luacs::PropertyKind kind =
        classify(type, native_width, array_count, element_size);
    width = native_width;
    if (element_size == 0) element_size = native_width;
    return kind;
}

} // namespace
