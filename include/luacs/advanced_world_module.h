#pragma once

#include "luacs/advanced_world_api.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace luacs {

using GetAdvancedWorldServicesFn = const AdvancedWorldServices* (*)();

inline const AdvancedWorldServices* resolve_advanced_world_services() {
    HMODULE core = GetModuleHandleW(L"luacs2.dll");
    if (!core) return nullptr;
    const auto get_services = reinterpret_cast<GetAdvancedWorldServicesFn>(
        GetProcAddress(core, "LuaCS_GetAdvancedWorldServices"));
    if (!get_services) return nullptr;
    const AdvancedWorldServices* services = get_services();
    if (!services ||
        services->abi_version != kAdvancedWorldServicesAbiVersion) {
        return nullptr;
    }
    return services;
}

} // namespace luacs
