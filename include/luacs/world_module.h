#pragma once

#include "luacs/module_api.h"
#include "luacs/world_api.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace luacs {

using GetWorldServicesFn = const WorldServices* (*)();

inline const WorldServices* resolve_world_services(const Services* services) {
    if (services && services->world &&
        services->world->abi_version == kWorldServicesAbiVersion) {
        return services->world;
    }

    HMODULE core = GetModuleHandleW(L"luacs2.dll");
    if (!core) return nullptr;
    const auto get_services = reinterpret_cast<GetWorldServicesFn>(
        GetProcAddress(core, "LuaCS_GetWorldServices"));
    if (!get_services) return nullptr;
    const WorldServices* world = get_services();
    if (!world || world->abi_version != kWorldServicesAbiVersion) {
        return nullptr;
    }
    return world;
}

} // namespace luacs
