#include "plugin.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <Color.h>
#include <igameevents.h>
#include <tier0/dbg.h>
#include <tier1/convar.h>

#include <cstdio>
#include <filesystem>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

SH_DECL_HOOK3_void(IServerGameDLL, GameFrame, SH_NOATTRIB, 0, bool, bool, bool);
SH_DECL_HOOK4_void(IServerGameClients, ClientActive, SH_NOATTRIB, 0,
                   CPlayerSlot, bool, const char*, uint64);
SH_DECL_HOOK5_void(IServerGameClients, ClientDisconnect, SH_NOATTRIB, 0,
                   CPlayerSlot, ENetworkDisconnectionReason, const char*,
                   uint64, const char*);
SH_DECL_HOOK4_void(IServerGameClients, ClientPutInServer, SH_NOATTRIB, 0,
                   CPlayerSlot, const char*, int, uint64);
SH_DECL_HOOK6_void(IServerGameClients, OnClientConnected, SH_NOATTRIB, 0,
                   CPlayerSlot, const char*, uint64, const char*, const char*,
                   bool);
SH_DECL_HOOK2_void(IServerGameClients, ClientCommand, SH_NOATTRIB, 0,
                   CPlayerSlot, const CCommand&);
SH_DECL_HOOK2(IGameEventManager2, FireEvent, SH_NOATTRIB, 0, bool,
              IGameEvent*, bool);

LuaCSPlugin g_LuaCSPlugin;
IServerGameDLL* g_server = nullptr;
IServerGameClients* g_game_clients = nullptr;
IVEngineServer* g_engine = nullptr;
IGameEventManager2* g_game_events = nullptr;
HMODULE g_lua_module = nullptr;
luacs::Runtime* g_runtime = nullptr;

namespace {

bool g_lua_command_registered = false;

std::filesystem::path module_path() {
    HMODULE module{};
    GetModuleHandleExW(
        GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
            GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(&module_path), &module);

    std::wstring buffer(32768, L'\0');
    const DWORD length = GetModuleFileNameW(
        module, buffer.data(), static_cast<DWORD>(buffer.size()));
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
    std::string message = length && buffer
                              ? std::string(buffer, length)
                              : "Windows error " + std::to_string(code);
    if (buffer) LocalFree(buffer);
    while (!message.empty() &&
           (message.back() == '\r' || message.back() == '\n' ||
            message.back() == ' ')) {
        message.pop_back();
    }
    return message;
}

bool preload_lua(const std::filesystem::path& native_directory,
                 std::string& error) {
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
        error = "Could not load " + lua_path.string() + ": " +
                windows_error(code) + " (" + std::to_string(code) + ")";
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

void command_lua(const CCommandContext&, const CCommand& command) {
    if (!g_runtime) {
        write_console("[ERROR] (lua) LuaCS runtime is not initialized.");
        return;
    }

    std::string command_line = "lua";
    for (int index = 1; index < command.ArgC(); ++index) {
        const char* argument = command.Arg(index);
        if (!argument) continue;
        command_line.push_back(' ');
        command_line += argument;
    }
    g_runtime->client_command(-1, command_line);
}

ConCommand g_lua_command(
    "lua", ConCommandCallbackInfo_t(&command_lua),
    "LuaCS runtime and plugin administration. Use 'lua help'.", FCVAR_RELEASE);

bool register_lua_command() {
    META_CONVAR_REGISTER(FCVAR_RELEASE);
    g_lua_command_registered =
        g_lua_command.IsValidRef() && g_lua_command.HasCallback();
    if (!g_lua_command_registered) ConVar_Unregister();
    return g_lua_command_registered;
}

void unregister_lua_command() {
    if (!g_lua_command_registered) return;
    g_SMAPI->UnregisterConCommand(g_PLAPI, &g_lua_command);
    ConVar_Unregister();
    g_lua_command_registered = false;
}

} // namespace

PLUGIN_EXPOSE(LuaCSPlugin, g_LuaCSPlugin);

bool LuaCSPlugin::Load(PluginId id, ISmmAPI* ismm, char* error, size_t maxlen,
                       bool late) {
    PLUGIN_SAVEVARS();

    write_console("[INFO] (lua) Native Metamod entry point loaded.");

    GET_V_IFACE_CURRENT(GetEngineFactory, g_engine, IVEngineServer,
                        INTERFACEVERSION_VENGINESERVER);
    GET_V_IFACE_CURRENT(GetEngineFactory, g_game_events, IGameEventManager2,
                        INTERFACEVERSION_GAMEEVENTSMANAGER2);
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
        const std::string message =
            "CS2 engine service initialization failed: " + game_api_error;
        write_console("[ERROR] (lua) " + message);
        copy_error(error, maxlen, message);
        return false;
    }
    game_api_.set_event_manager(g_game_events);
    if (!game_api_error.empty()) {
        write_console("[WARN] (lua) " + game_api_error);
    }
    write_console(
        "[INFO] (lua) Initialized live player, event, HUD, cvar, and inventory services.");

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
        const std::string message = runtime_error.empty()
                                        ? "LuaCS initialization failed."
                                        : runtime_error;
        write_console("[ERROR] (lua) " + message);
        copy_error(error, maxlen, message);
        return false;
    }
    runtime_.set_host_operations(game_api_.host_operations());
    g_runtime = &runtime_;

    if (!register_lua_command()) {
        g_runtime = nullptr;
        runtime_.shutdown();
        game_api_.shutdown();
        const std::string message =
            "Could not register the server-console 'lua' command through the "
            "Source 2 convar system.";
        write_console("[ERROR] (lua) " + message);
        copy_error(error, maxlen, message);
        return false;
    }

    fire_event_pre_hook_id_ = SH_ADD_HOOK(
        IGameEventManager2, FireEvent, g_game_events,
        SH_MEMBER(this, &LuaCSPlugin::Hook_FireEvent), false);
    fire_event_post_hook_id_ = SH_ADD_HOOK(
        IGameEventManager2, FireEvent, g_game_events,
        SH_MEMBER(this, &LuaCSPlugin::Hook_FireEventPost), true);
    game_frame_hook_id_ = SH_ADD_HOOK(
        IServerGameDLL, GameFrame, g_server,
        SH_MEMBER(this, &LuaCSPlugin::Hook_GameFrame), true);
    client_active_hook_id_ = SH_ADD_HOOK(
        IServerGameClients, ClientActive, g_game_clients,
        SH_MEMBER(this, &LuaCSPlugin::Hook_ClientActive), true);
    client_disconnect_hook_id_ = SH_ADD_HOOK(
        IServerGameClients, ClientDisconnect, g_game_clients,
        SH_MEMBER(this, &LuaCSPlugin::Hook_ClientDisconnect), true);
    client_put_in_server_hook_id_ = SH_ADD_HOOK(
        IServerGameClients, ClientPutInServer, g_game_clients,
        SH_MEMBER(this, &LuaCSPlugin::Hook_ClientPutInServer), true);
    client_connected_hook_id_ = SH_ADD_HOOK(
        IServerGameClients, OnClientConnected, g_game_clients,
        SH_MEMBER(this, &LuaCSPlugin::Hook_OnClientConnected), false);
    client_command_hook_id_ = SH_ADD_HOOK(
        IServerGameClients, ClientCommand, g_game_clients,
        SH_MEMBER(this, &LuaCSPlugin::Hook_ClientCommand), false);

    std::vector<std::string_view> failed_hooks;
    if (fire_event_pre_hook_id_ <= 0)
        failed_hooks.push_back("IGameEventManager2::FireEvent pre");
    if (fire_event_post_hook_id_ <= 0)
        failed_hooks.push_back("IGameEventManager2::FireEvent post");
    if (game_frame_hook_id_ <= 0)
        failed_hooks.push_back("IServerGameDLL::GameFrame");
    if (client_active_hook_id_ <= 0)
        failed_hooks.push_back("IServerGameClients::ClientActive");
    if (client_disconnect_hook_id_ <= 0)
        failed_hooks.push_back("IServerGameClients::ClientDisconnect");
    if (client_put_in_server_hook_id_ <= 0)
        failed_hooks.push_back("IServerGameClients::ClientPutInServer");
    if (client_connected_hook_id_ <= 0)
        failed_hooks.push_back("IServerGameClients::OnClientConnected");
    if (client_command_hook_id_ <= 0)
        failed_hooks.push_back("IServerGameClients::ClientCommand");

    if (!failed_hooks.empty()) {
        remove_hooks();
        unregister_lua_command();
        g_runtime = nullptr;
        runtime_.shutdown();
        game_api_.shutdown();

        std::ostringstream message;
        message << "Could not install required Source 2 hook(s): ";
        for (std::size_t index = 0; index < failed_hooks.size(); ++index) {
            if (index != 0) message << ", ";
            message << failed_hooks[index];
        }
        write_console("[ERROR] (lua) " + message.str());
        copy_error(error, maxlen, message.str());
        return false;
    }

    g_SMAPI->AddListener(this, this);
    write_console("[INFO] (lua) Installed all 8 required Source 2 hooks.");
    runtime_.load_plugins();
    write_console("[INFO] (lua) LuaCS startup completed successfully.");
    return true;
}

bool LuaCSPlugin::Unload(char*, size_t) {
    remove_hooks();
    unregister_lua_command();
    g_runtime = nullptr;
    free_event_copies();
    runtime_.shutdown();
    game_api_.shutdown();
    g_game_events = nullptr;
    write_console("[INFO] (lua) LuaCS unloaded.");
    return true;
}

void LuaCSPlugin::remove_hooks() {
    const auto remove = [](int& hook_id) {
        if (hook_id > 0) SH_REMOVE_HOOK_ID(hook_id);
        hook_id = -1;
    };
    remove(game_frame_hook_id_);
    remove(client_active_hook_id_);
    remove(client_disconnect_hook_id_);
    remove(client_put_in_server_hook_id_);
    remove(client_connected_hook_id_);
    remove(client_command_hook_id_);
    remove(fire_event_pre_hook_id_);
    remove(fire_event_post_hook_id_);
}

void LuaCSPlugin::AllPluginsLoaded() {}

void LuaCSPlugin::OnLevelInit(const char* map_name, const char*, const char*,
                              const char*, bool, bool) {
    runtime_.level_init(map_name ? map_name : "");
}

void LuaCSPlugin::OnLevelShutdown() {
    runtime_.level_shutdown();
}

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

void LuaCSPlugin::Hook_ClientPutInServer(CPlayerSlot slot, const char* name,
                                         int, uint64 xuid) {
    runtime_.player_put_in_server(slot.Get(), name ? name : "", xuid);
}

void LuaCSPlugin::Hook_OnClientConnected(CPlayerSlot slot, const char* name,
                                         uint64 xuid, const char* network_id,
                                         const char*, bool fake_player) {
    runtime_.player_connected(slot.Get(), name ? name : "", xuid,
                              network_id ? network_id : "", fake_player);
}

void LuaCSPlugin::Hook_ClientCommand(CPlayerSlot slot,
                                     const CCommand& command) {
    runtime_.client_command(slot.Get(), command.GetCommandString());
}

bool LuaCSPlugin::Hook_FireEvent(IGameEvent* event, bool dont_broadcast) {
    auto* manager = META_IFACEPTR(IGameEventManager2);
    if (!manager) manager = g_game_events;
    game_api_.set_event_manager(manager);

    if (!manager || !event) {
        event_copies_.push_back(nullptr);
        RETURN_META_VALUE(MRES_IGNORED, true);
    }

    event_copies_.push_back(manager->DuplicateEvent(event));
    const std::uint64_t token =
        game_api_.begin_event(event, false, dont_broadcast);
    runtime_.dispatch_game_event(token, event->GetName(), event->GetID(),
                                 event->IsReliable(), event->IsLocal(), false,
                                 dont_broadcast);
    const LuaCSEventDecision decision = game_api_.end_event(token);

    if (decision.cancelled) {
        manager->FreeEvent(event);
        RETURN_META_VALUE(MRES_SUPERCEDE, false);
    }
    if (decision.dont_broadcast != dont_broadcast) {
        RETURN_META_VALUE_NEWPARAMS(
            MRES_IGNORED, true, &IGameEventManager2::FireEvent,
            (event, decision.dont_broadcast));
    }
    RETURN_META_VALUE(MRES_IGNORED, true);
}

bool LuaCSPlugin::Hook_FireEventPost(IGameEvent*, bool dont_broadcast) {
    IGameEvent* copy = nullptr;
    if (!event_copies_.empty()) {
        copy = event_copies_.back();
        event_copies_.pop_back();
    }

    auto* manager = game_api_.event_manager();
    if (copy) {
        const std::uint64_t token =
            game_api_.begin_event(copy, true, dont_broadcast);
        runtime_.dispatch_game_event(token, copy->GetName(), copy->GetID(),
                                     copy->IsReliable(), copy->IsLocal(), true,
                                     dont_broadcast);
        game_api_.end_event(token);
        if (manager) manager->FreeEvent(copy);
    }
    RETURN_META_VALUE(MRES_IGNORED, true);
}

void LuaCSPlugin::free_event_copies() {
    auto* manager = game_api_.event_manager();
    if (manager) {
        for (IGameEvent* event : event_copies_) {
            if (event) manager->FreeEvent(event);
        }
    }
    event_copies_.clear();
}
