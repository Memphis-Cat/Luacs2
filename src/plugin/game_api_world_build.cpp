#include "luacs/world_api.h"

#include <cstddef>

namespace {

template <typename Function>
Function luacs_resolve_virtual_function(void* instance, std::size_t index) {
    if (!instance) return nullptr;
    auto** vtable = *reinterpret_cast<void***>(instance);
    if (!vtable) return nullptr;
    return reinterpret_cast<Function>(vtable[index]);
}

} // namespace

// The implementation calls the public entity-get operation by its internal
// entity_info method name. WorldServices deliberately exposes both names as a
// union alias, so this translation unit can map the implementation name without
// changing the ABI layout used by modules.
#define entity_get entity_info
#define virtual_function luacs_resolve_virtual_function
#include "game_api_world.cpp"
#undef virtual_function
#undef entity_get
