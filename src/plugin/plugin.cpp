#include "plugin.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstdio>
#include <filesystem>
#include <string>

SH_DECL_HOOK3_void(IServerGameDLL, GameFrame, SH_NOATTRIB, 0, bool, bool, bool);
SH_DECL_HOOK4_void(IServerGameClients, ClientActive, SH_NOATTRIB, 0, CPlayerSlot, bool, const char*, uint64);
SH_DECL_HOOK5_void(IServerGameClients, ClientDisconnect, SH_NOATTRIB, 0, CPlayerSlot,
                   ENetworkDisconnectionReason, const char*, uint64, const char*);
SH_DECL_HOOK4_void(IServerGameClients, ClientPutInServer, SH_NOATTRIB, 0, CPlayerSlot, const char*, int, uint64);
SH_DECL_HOOK6_void(IServerGameClients, OnClientConnected, SH_NOATTRIB, 0, CPlayerSlot,
                   const char*, uint64, const char*, const char*, bool);
SH_DECL_HOOK2_void(IServerGameClients, ClientCommand, SH_NOATTRIB, 0, CPlayerSlot, const CCommand&);

LuaCSPlugin g_LuaCSPlugin;
IServerGameDLL* g_server = nullptr;
IServerGameClients* g_game_clients = nullptr;
IVEngineServer* g_engine = nullptr;

namespace {
std::filesystem::path module_path() {
    HMODULE module{};
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                       GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(&module_path), &module);
    std::wstring buffer(32768, L'\0');
    const DWORD length = GetModuleFileNameW(module, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (!length || length >= buffer.size()) return {};
    buffer.resize(length);
    return buffer;
}

void copy_error(char* destination, size_t maximum, const std::string& value) {
    if (!destination || maximum == 0) return;
    std::snprintf(destination, maximum, "%s", value.c_str());
}
}

PLUGIN_EXPOSE(LuaCSPlugin, g_LuaCSPlugin);

bool LuaCSPlugin::Load(PluginId id, ISmmAPI* ismm, char* error, size_t maxlen, bool late) {
    PLUGIN_SAVEVARS();
    GET_V_IFACE_CURRENT(GetEngineFactory, g_engine, IVEngineServer, INTERFACEVERSION_VENGINESERVER);
    GET_V_IFACE_ANY(GetServerFactory, g_server, IServerGameDLL, INTERFACEVERSION_SERVERGAMEDLL);
    GET_V_IFACE_ANY(GetServerFactory, g_game_clients, IServerGameClients, INTERFACEVERSION_SERVERGAMECLIENTS);

    const auto dll = module_path();
    const auto root = dll.empty() ? std::filesystem::path{} : dll.parent_path().parent_path();
    std::string runtime_error;
    if (root.empty() || !runtime_.initialize(
            root,
            [](std::string_view text) { META_CONPRINTF("%.*s\n", static_cast<int>(text.size()), text.data()); },
            [](std::string_view command) {
                if (!g_engine) return;
                std::string owned(command);
                if (owned.empty() || owned.back() != '\n') owned.push_back('\n');
                g_engine->ServerCommand(owned.c_str());
            },
            runtime_error)) {
        copy_error(error, maxlen, runtime_error.empty() ? "LuaCS initialization failed" : runtime_error);
        return false;
    }

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

    runtime_.load_plugins();
    META_CONPRINTF("[LuaCS] Loaded runtime from %s\n", root.string().c_str());
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
    return true;
}

void LuaCSPlugin::AllPluginsLoaded() {}

void LuaCSPlugin::OnLevelInit(const char* map_name, const char*, const char*, const char*, bool, bool) {
    runtime_.level_init(map_name ? map_name : "");
}

void LuaCSPlugin::OnLevelShutdown() { runtime_.level_shutdown(); }

void LuaCSPlugin::Hook_GameFrame(bool simulating, bool, bool) {
    if (simulating) runtime_.tick();
}

void LuaCSPlugin::Hook_ClientActive(CPlayerSlot slot, bool, const char* name, uint64 xuid) {
    runtime_.player_active(slot.Get(), name ? name : "", xuid);
}

void LuaCSPlugin::Hook_ClientDisconnect(CPlayerSlot slot, ENetworkDisconnectionReason,
                                        const char* name, uint64 xuid, const char* network_id) {
    runtime_.player_disconnected(slot.Get(), name ? name : "", xuid, network_id ? network_id : "");
}

void LuaCSPlugin::Hook_ClientPutInServer(CPlayerSlot slot, const char* name, int, uint64 xuid) {
    runtime_.player_put_in_server(slot.Get(), name ? name : "", xuid);
}

void LuaCSPlugin::Hook_OnClientConnected(CPlayerSlot slot, const char* name, uint64 xuid,
                                         const char* network_id, const char*, bool fake_player) {
    runtime_.player_connected(slot.Get(), name ? name : "", xuid,
                              network_id ? network_id : "", fake_player);
}

void LuaCSPlugin::Hook_ClientCommand(CPlayerSlot slot, const CCommand& command) {
    runtime_.client_command(slot.Get(), command.GetCommandString());
}
