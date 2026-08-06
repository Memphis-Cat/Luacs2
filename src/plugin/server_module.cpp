#include "server_module.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <algorithm>
#include <cwchar>
#include <cwctype>
#include <string>

namespace {

HMODULE g_game_server_module = nullptr;
std::filesystem::path g_game_server_module_path;

std::string windows_error_code(DWORD code) {
    return "Windows error " +
           std::to_string(static_cast<unsigned long>(code));
}

} // namespace

bool LuaCSBindGameServerModule(void* server_interface, std::string& error) {
    g_game_server_module = nullptr;
    g_game_server_module_path.clear();
    error.clear();

    if (!server_interface) {
        error = "IServerGameDLL interface pointer is null";
        return false;
    }

    auto** vtable = *reinterpret_cast<void***>(server_interface);
    if (!vtable || !vtable[0]) {
        error = "IServerGameDLL has no usable virtual table entry";
        return false;
    }

    HMODULE module = nullptr;
    if (!GetModuleHandleExW(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(vtable[0]), &module) ||
        !module) {
        error = "Could not resolve the module containing the live "
                "IServerGameDLL virtual table: " +
                windows_error_code(GetLastError());
        return false;
    }

    wchar_t module_path[32768]{};
    constexpr DWORD module_path_capacity = static_cast<DWORD>(
        sizeof(module_path) / sizeof(module_path[0]));
    const DWORD path_length =
        GetModuleFileNameW(module, module_path, module_path_capacity);
    if (path_length == 0 || path_length >= module_path_capacity) {
        error = "Could not resolve the live IServerGameDLL module path: " +
                windows_error_code(GetLastError());
        return false;
    }

    const std::filesystem::path resolved_path(module_path);
    if (_wcsicmp(resolved_path.filename().c_str(), L"server.dll") != 0) {
        error = "The live IServerGameDLL virtual table resolved to an "
                "unexpected module: " +
                resolved_path.string();
        return false;
    }

    std::wstring normalized_path = resolved_path.native();
    std::transform(
        normalized_path.begin(), normalized_path.end(), normalized_path.begin(),
        [](wchar_t value) {
            return static_cast<wchar_t>(std::towlower(value));
        });
    if (normalized_path.find(L"\\addons\\metamod\\") !=
        std::wstring::npos) {
        error = "The live IServerGameDLL virtual table resolved to Metamod's "
                "proxy server.dll instead of CS2's game server module: " +
                resolved_path.string();
        return false;
    }

    g_game_server_module = module;
    g_game_server_module_path = resolved_path;
    return true;
}

void* LuaCSGameServerModule() {
    return g_game_server_module;
}

const std::filesystem::path& LuaCSGameServerModulePath() {
    return g_game_server_module_path;
}
