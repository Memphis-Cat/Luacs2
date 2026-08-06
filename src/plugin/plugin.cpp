#include "plugin.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <Color.h>
#include <tier0/dbg.h>

#include <cstdio>
#include <filesystem>
#include <string>
#include <string_view>

SH_DECL_HOOK3_void(IServerGameDLL, GameFrame, SH_NOATTRIB, 0, bool, bool, bool);
SH_DECL_HOOK4_void(IServerGameClients, ClientActive, SH_NOATTRIB, 0, CPlayerSlot, bool,
                   const char*, uint64);
SH_DECL_HOOK5_void(IServerGameClients, ClientDisconnect, SH_NOATTRIB, 0, CPlayerSlot,
                   ENetworkDisconnectionReason, const char*, uint64, const char*);
SH_DECL_HOOK4_void(IServerGameClients, ClientPutInServer, SH_NOATTRIB, 0, CPlayerSlot,
                   const char*, int, uint64);
SH_DECL_HOOK6_void(IServerGameClients, OnClientConnected, SH_NOATTRIB, 0, CPlayerSlot,
                   const char*, uint64, const char*, const char*, bool);
SH_DECL_HOOK2_void(IServerGameClients, ClientCommand, SH_NOATTRIB, 0, CPlayerSlot,
                   const CCommand&);

LuaCSPlugin g_LuaCSPlugin;
IServerGameDLL* g_server = nullptr;
IServerGameClients* g_game_clients = nullptr;
IVEngineServer* g_engine = nullptr;
HMODULE g_lua_module = nullptr;

namespace {

std::filesystem::path module_path() {
    HMODULE module{};
    GetModuleHandleExW(
        GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
            GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(&module_path), &module);

    std::wstring buffer(32768, L'\0');
    const DWORD length =
        GetModuleFileNameW(module, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (!length || length >= buffer.size()) return {};
    buffer.resize(length);
    return buffer;
}

std::filesystem::path find_luacs_root(const std::filesystem::path& dll) {
    auto current = dll.parent_path();
    while (!current.empty()) {
        if (_wcsicmp(current.filename().c_str(), L"LuaCS") == 0) return current;
        const auto parent = current.parent_path();
        if (parent == current) break;
        current = parent;
    }
    return {};
}

std::string windows_error(DWORD code) {
    char* buffer = nullptr;
    const DWORD length = FormatMessageA(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
            FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, code, 0, reinterpret_cast<char*>(&buffer), 0, nullptr);
    std::string message =
        length && buffer ? std::string(buffer, length) : "Windows error " + std::to_string(code);
    if (buffer) LocalFree(buffer);
    while (!message.empty() &&
           (message.back() == '\r' || message.back() == '\n' || message.back() == ' ')) {
        message.pop_back();
    }
    return message;
}

bool preload_lua(const std::filesystem::path& native_directory, std::string& error) {
    const auto lua_path = native_directory / "lua55.dll";
    if (!std::filesystem::exists(lua_path)) {
        error = "Required dependency was not found: " + lua_path.string();
        return false;
    }

    g_lua_module = LoadLibraryExW(
        lua_path.c_str(), nullptr,
        LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if (!g_lua_module) {
        const DWORD code = GetLastError();
        error = "Could not load " + lua_path.string() + ": " + windows_error(code) +
                " (" + std::to_string(code) + ")";
        return false;
    }
    return true;
}

void copy_error(char* destination, size_t maximum, const std::string& value) {
    if (!destination || maximum == 0) return;
    std::snprintf(destination, maximum, "%s", value.c_str());
}

Color line_color(std::string_view line) {
    if (line.find("[ERROR]") != std::string_view::npos) {
        return Color(255, 90, 90, 255);
    }
    if (line.find("[WARN]") != std::string_view::npos) {
        return Color(255, 205, 70, 255);
    }
    if (line.find("[DEBUG]") != std::string_view::npos) {
        return Color(155, 165, 180, 255);
    }
    if (line.find("(lua:") != std::string_view::npos) {
        return Color(145, 225, 170, 255);
    }
    return Color(105, 205, 255, 255);
}

void write_console(std::string_view line) {
    const auto color = line_color(line);
    ConColorMsg(color, "%.*s\n", static_cast<int>(line.size()), line.data());
}

} // namespace

PLUGIN_EXPOSE(LuaCSPlugin, g_LuaCSPlugin);

bool LuaCSPlugin::Load(PluginId id, ISmmAPI* ismm, char* error, size_t maxlen,
                       bool late) {
    PLUGIN_SAVEVARS();

    write_console("[INFO] (lua) Native Metamod entry point loaded.");

    GET_V_IFACE_CURRENT(GetEngineFactory, g_engine, IVEngineServer,
                        INTERFACEVERSION_VENGINESERVER);
    GET_V_IFACE_ANY(GetServerFactory, g_server, IServerGameDLL,
                    INTERFACEVERSION_SERVERGAMEDLL);
    GET_V_IFACE_ANY(GetServerFactory, g_game_clients, IServerGameClients,
                    INTERFACEVERSION_SERVERGAMECLIENTS);

    write_console("[INFO] (lua) Connected to Source 2 server interfaces.");

    const auto dll = module_path();
    const auto root = find_luacs_root(dll);
    if (dll.empty() || root.empty()) {
        const std::string message =
            "Could not resolve the LuaCS installation root from the native DLL path.";
        write_console("[ERROR] (lua) " + message);
        copy_error(error, maxlen, message);
        return false;
    }

    std::string dependency_error;
    if (!preload_lua(dll.parent_path(), dependency_error)) {
        write_console("[ERROR] (lua) " + dependency_error);
        copy_error(error, maxlen, dependency_error);
        return false;
    }
    write_console("[INFO] (lua) Loaded Lua 5.5.1 native dependency from " +
                  (dll.parent_path() / "lua55.dll").string());

    std::string game_api_error;
    if (!game_api_.initialize(root, game_api_error)) {
        const std::string message = "CS2 engine service initialization failed: " +
                                    game_api_error;
        write_console("[ERROR] (lua) " + message);
        copy_error(error, maxlen, message);
        return false;
    }
    if (!game_api_error.empty()) {
        write_console("[WARN] (lua) " + game_api_error);
    }
    write_console("[INFO] (lua) Initialized HUD and cvar services; entity-backed operations resolve lazily.");

    std::string runtime_error;
    if (!runtime_.initialize(
            root, [](std::string_view text) { write_console(text); },
            [](std::string_view command) {
                if (!g_engine) return;
                std::string owned(command);
                if (owned.empty() || owned.back() != '\n') owned.push_back('\n');
                g_engine->ServerCommand(owned.c_str());
            },
            runtime_error)) {
        game_api_.shutdown();
        const std::string message =
            runtime_error.empty() ? "LuaCS initialization failed." : runtime_error;
        write_console("[ERROR] (lua) " + message);
        copy_error(error, maxlen, message);
        return false;
    }
    runtime_.set_host_operations(game_api_.host_operations());

    g_SMAPI->AddListener(this, this);
    SH_ADD_HOOK(IServerGameDLL, GameFrame, g_server,
                SH_MEMBER(this, &LuaCSPlugin::Hook_GameFrame), true);
    SH_ADD_HOOK(IServerGameClients, ClientActive, g_game_clients,
                SH_MEMBER(this, &LuaCSPlugin::Hook_ClientActive), true);
    SH_ADD_HOOK(IServerGameClients, ClientDisconnect, g_game_clients,
                SH_MEMBER(this, &LuaCSPlugin::Hook_ClientDisconnect), true);
    SH_ADD_HOOK(IServerGameClients, ClientPutInServer, g_game_clients,
                SH_MEMBER(this, &LuaCSPlugin::Hook_ClientPutInServer), true);
    SH_ADD_HOOK(IServerGameClients, OnClientConnected, g_game_clients,
                SH_MEMBER(this, &LuaCSPlugin::Hook_OnClientConnected), false);
    SH_ADD_HOOK(IServerGameClients, ClientCommand, g_game_clients,
                SH_MEMBER(this, &LuaCSPlugin::Hook_ClientCommand), false);

    write_console("[INFO] (lua) Installed 6 Source 2 server hooks.");
    runtime_.load_plugins();
    write_console("[INFO] (lua) LuaCS startup completed successfully.");
    return true;
}

bool LuaCSPlugin::Unload(char*, size_t) {
    SH_REMOVE_HOOK(IServerGameDLL, GameFrame, g_server,
                   SH_MEMBER(this, &LuaCSPlugin::Hook_GameFrame), true);
    SH_REMOVE_HOOK(IServerGameClients, ClientActive, g_game_clients,
                   SH_MEMBER(this, &LuaCSPlugin::Hook_ClientActive), true);
    SH_REMOVE_HOOK(IServerGameClients, ClientDisconnect, g_game_clients,
                   SH_MEMBER(this, &LuaCSPlugin::Hook_ClientDisconnect), true);
    SH_REMOVE_HOOK(IServerGameClients, ClientPutInServer, g_game_clients,
                   SH_MEMBER(this, &LuaCSPlugin::Hook_ClientPutInServer), true);
    SH_REMOVE_HOOK(IServerGameClients, OnClientConnected, g_game_clients,
                   SH_MEMBER(this, &LuaCSPlugin::Hook_OnClientConnected), false);
    SH_REMOVE_HOOK(IServerGameClients, ClientCommand, g_game_clients,
                   SH_MEMBER(this, &LuaCSPlugin::Hook_ClientCommand), false);
    runtime_.shutdown();
    game_api_.shutdown();
    write_console("[INFO] (lua) LuaCS unloaded.");
    return true;
}

void LuaCSPlugin::AllPluginsLoaded() {}

void LuaCSPlugin::OnLevelInit(const char* map_name, const char*, const char*,
                              const char*, bool, bool) {
    runtime_.level_init(map_name ? map_name : "");
}

void LuaCSPlugin::OnLevelShutdown() { runtime_.level_shutdown(); }

void LuaCSPlugin::Hook_GameFrame(bool simulating, bool, bool) {
    if (simulating) runtime_.tick();
}

void LuaCSPlugin::Hook_ClientActive(CPlayerSlot slot, bool, const char* name,
                                    uint64 xuid) {
    runtime_.player_active(slot.Get(), name ? name : "", xuid);
}

void LuaCSPlugin::Hook_ClientDisconnect(CPlayerSlot slot,
                                        ENetworkDisconnectionReason,
                                        const char* name, uint64 xuid,
                                        const char* network_id) {
    runtime_.player_disconnected(slot.Get(), name ? name : "", xuid,
                                 network_id ? network_id : "");
}

void LuaCSPlugin::Hook_ClientPutInServer(CPlayerSlot slot, const char* name, int,
                                         uint64 xuid) {
    runtime_.player_put_in_server(slot.Get(), name ? name : "", xuid);
}

void LuaCSPlugin::Hook_OnClientConnected(CPlayerSlot slot, const char* name,
                                         uint64 xuid, const char* network_id,
                                         const char*, bool fake_player) {
    runtime_.player_connected(slot.Get(), name ? name : "", xuid,
                              network_id ? network_id : "", fake_player);
}

void LuaCSPlugin::Hook_ClientCommand(CPlayerSlot slot, const CCommand& command) {
    runtime_.client_command(slot.Get(), command.GetCommandString());
}
