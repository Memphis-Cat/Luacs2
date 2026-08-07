#include "plugin.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <Color.h>
#include <igameevents.h>
#include <tier0/dbg.h>
#include <tier1/convar.h>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>

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
SH_DECL_HOOK2(IGameEventManager2, LoadEventsFromFile, SH_NOATTRIB, 0, int,
              const char*, bool);
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
std::filesystem::path g_native_error_log;

std::string path_text(const std::filesystem::path& path) {
    const auto utf8 = path.u8string();
    return {reinterpret_cast<const char*>(utf8.data()), utf8.size()};
}

std::filesystem::path module_path() {
    HMODULE module{};
    if (!GetModuleHandleExW(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(&module_path), &module) ||
        !module) {
        return {};
    }

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

void release_lua_dependency() {
    if (!g_lua_module) return;
    FreeLibrary(g_lua_module);
    g_lua_module = nullptr;
}

bool preload_lua(const std::filesystem::path& native_directory,
                 std::string& error) {
    release_lua_dependency();
    const auto lua_path = native_directory / "lua55.dll";
    std::error_code exists_error;
    const bool exists = std::filesystem::is_regular_file(lua_path, exists_error);
    if (exists_error || !exists) {
        error = exists_error
                    ? "Could not inspect required Lua dependency: " +
                          exists_error.message()
                    : "Required dependency was not found: " + path_text(lua_path);
        return false;
    }

    g_lua_module = LoadLibraryExW(
        lua_path.c_str(), nullptr,
        LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if (!g_lua_module) {
        const DWORD code = GetLastError();
        error = "Could not load " + path_text(lua_path) + ": " +
                windows_error(code) + " (" + std::to_string(code) + ")";
        return false;
    }
    return true;
}

void copy_error(char* destination, size_t maximum, const std::string& value) {
    if (!destination || maximum == 0) return;
    const std::size_t copy = std::min(value.size(), maximum - 1);
    if (copy != 0) std::memcpy(destination, value.data(), copy);
    destination[copy] = '\0';
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

void append_native_error(std::string_view line) {
    if (g_native_error_log.empty() ||
        line.find("[ERROR]") == std::string_view::npos) {
        return;
    }

    std::error_code directory_error;
    std::filesystem::create_directories(g_native_error_log.parent_path(),
                                        directory_error);
    if (directory_error) return;

    std::ofstream output(g_native_error_log,
                         std::ios::app | std::ios::binary);
    if (!output) return;

    SYSTEMTIME now{};
    GetLocalTime(&now);
    char timestamp[32]{};
    std::snprintf(timestamp, sizeof(timestamp),
                  "%04u-%02u-%02u %02u:%02u:%02u ",
                  static_cast<unsigned>(now.wYear),
                  static_cast<unsigned>(now.wMonth),
                  static_cast<unsigned>(now.wDay),
                  static_cast<unsigned>(now.wHour),
                  static_cast<unsigned>(now.wMinute),
                  static_cast<unsigned>(now.wSecond));
    output.write(timestamp, static_cast<std::streamsize>(std::strlen(timestamp)));
    if (!line.empty()) {
        output.write(line.data(), static_cast<std::streamsize>(
                                      std::min<std::size_t>(line.size(), 1u << 20))));
    }
    output.put('\n');
}

void write_console(std::string_view line) {
    append_native_error(line);
    const auto color = line_color(line);
    constexpr std::size_t kConsoleChunk = 4096;
    if (line.empty()) {
        ConColorMsg(color, "\n");
        return;
    }
    std::size_t offset = 0;
    while (offset < line.size()) {
        const std::size_t chunk =
            std::min(kConsoleChunk, line.size() - offset);
        ConColorMsg(color, "%.*s", static_cast<int>(chunk),
                    line.data() + offset);
        offset += chunk;
    }
    ConColorMsg(color, "\n");
}

void command_lua(const CCommandContext&, const CCommand& command) {
    if (!g_runtime) {
        write_console("[ERROR] (lua) LuaCS runtime is not initialized.");
        return;
    }

    try {
        std::string command_line = "lua";
        for (int index = 1; index < command.ArgC(); ++index) {
            const char* argument = command.Arg(index);
            if (!argument) continue;
            command_line.push_back(' ');
            command_line += argument;
        }
        g_runtime->client_command(-1, command_line);
    } catch (const std::exception& exception) {
        write_console("[ERROR] (lua) Console command handling threw: " +
                      std::string(exception.what()));
    } catch (...) {
        write_console("[ERROR] (lua) Console command handling threw an unknown exception.");
    }
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
    g_native_error_log = root / "logs" / "luacs-errors.log";

    std::string dependency_error;
    if (!preload_lua(dll.parent_path(), dependency_error)) {
        write_console("[ERROR] (lua) " + dependency_error);
        copy_error(error, maxlen, dependency_error);
        return false;
    }
    write_console("[INFO] (lua) Loaded Lua 5.5.1 native dependency from " +
                  path_text(dll.parent_path() / "lua55.dll"));

    std::string game_api_error;
    if (!game_api_.initialize(root, game_api_error)) {
        release_lua_dependency();
        const std::string message =
            "CS2 engine service initialization failed: " + game_api_error;
        write_console("[ERROR] (lua) " + message);
        copy_error(error, maxlen, message);
        return false;
    }

    auto* event_manager_vtable = reinterpret_cast<IGameEventManager2*>(
        game_api_.event_manager_vtable());
    if (!event_manager_vtable) {
        game_api_.shutdown();
        release_lua_dependency();
        const std::string message =
            "Could not resolve the CGameEventManager virtual table required "
            "for Source 2 dynamic virtual hooks.";
        write_console("[ERROR] (lua) " + message);
        copy_error(error, maxlen, message);
        return false;
    }

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
        release_lua_dependency();
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
        release_lua_dependency();
        const std::string message =
            "Could not register the server-console 'lua' command through the "
            "Source 2 convar system.";
        write_console("[ERROR] (lua) " + message);
        copy_error(error, maxlen, message);
        return false;
    }

    load_events_hook_id_ = SH_ADD_DVPHOOK(
        IGameEventManager2, LoadEventsFromFile, event_manager_vtable,
        SH_MEMBER(this, &LuaCSPlugin::Hook_LoadEventsFromFile), false);
    fire_event_pre_hook_id_ = SH_ADD_DVPHOOK(
        IGameEventManager2, FireEvent, event_manager_vtable,
        SH_MEMBER(this, &LuaCSPlugin::Hook_FireEvent), false);
    fire_event_post_hook_id_ = SH_ADD_DVPHOOK(
        IGameEventManager2, FireEvent, event_manager_vtable,
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

    std::ostringstream failed_hooks;
    const auto append_failed_hook = [&](const char* name, int hook_id) {
        if (hook_id > 0) return;
        if (failed_hooks.tellp() > 0) failed_hooks << ", ";
        failed_hooks << name;
    };
    append_failed_hook("IGameEventManager2::LoadEventsFromFile",
                       load_events_hook_id_);
    append_failed_hook("IGameEventManager2::FireEvent(pre)",
                       fire_event_pre_hook_id_);
    append_failed_hook("IGameEventManager2::FireEvent(post)",
                       fire_event_post_hook_id_);
    append_failed_hook("IServerGameDLL::GameFrame", game_frame_hook_id_);
    append_failed_hook("IServerGameClients::ClientActive",
                       client_active_hook_id_);
    append_failed_hook("IServerGameClients::ClientDisconnect",
                       client_disconnect_hook_id_);
    append_failed_hook("IServerGameClients::ClientPutInServer",
                       client_put_in_server_hook_id_);
    append_failed_hook("IServerGameClients::OnClientConnected",
                       client_connected_hook_id_);
    append_failed_hook("IServerGameClients::ClientCommand",
                       client_command_hook_id_);

    const std::string failed_hook_text = failed_hooks.str();
    if (!failed_hook_text.empty()) {
        remove_hooks();
        unregister_lua_command();
        free_event_copies();
        g_runtime = nullptr;
        runtime_.shutdown();
        game_api_.shutdown();
        release_lua_dependency();
        const std::string message =
            "Could not install required Source 2 hook(s): " +
            failed_hook_text;
        write_console("[ERROR] (lua) " + message);
        copy_error(error, maxlen, message);
        return false;
    }

    write_console("[INFO] (lua) Installed all 9 required Source 2 hooks.");
    if (late) {
        write_console("[INFO] (lua) LuaCS was loaded late; current players "
                      "will be discovered by subsequent Source 2 callbacks.");
    }

    try {
        runtime_.load_plugins();
    } catch (const std::exception& exception) {
        const std::string message =
            "Plugin discovery/loading threw: " + std::string(exception.what());
        write_console("[ERROR] (lua) " + message);
        remove_hooks();
        unregister_lua_command();
        free_event_copies();
        g_runtime = nullptr;
        runtime_.shutdown();
        game_api_.shutdown();
        release_lua_dependency();
        copy_error(error, maxlen, message);
        return false;
    } catch (...) {
        const std::string message =
            "Plugin discovery/loading threw an unknown C++ exception.";
        write_console("[ERROR] (lua) " + message);
        remove_hooks();
        unregister_lua_command();
        free_event_copies();
        g_runtime = nullptr;
        runtime_.shutdown();
        game_api_.shutdown();
        release_lua_dependency();
        copy_error(error, maxlen, message);
        return false;
    }
    return true;
}

bool LuaCSPlugin::Unload(char* error, size_t maxlen) {
    (void)error;
    (void)maxlen;
    write_console("[INFO] (lua) LuaCS is unloading.");
    remove_hooks();
    unregister_lua_command();
    free_event_copies();
    g_runtime = nullptr;
    runtime_.shutdown();
    game_api_.shutdown();
    release_lua_dependency();
    g_game_events = nullptr;
    g_game_clients = nullptr;
    g_server = nullptr;
    g_engine = nullptr;
    g_native_error_log.clear();
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

int LuaCSPlugin::Hook_LoadEventsFromFile(const char*, bool) {
    IGameEventManager2* manager = META_IFACEPTR(IGameEventManager2);
    if (manager && manager != g_game_events) {
        g_game_events = manager;
        game_api_.set_game_event_manager(manager);
        write_console("[INFO] (lua) Captured the live IGameEventManager2 "
                      "instance.");
    }
    RETURN_META_VALUE(MRES_IGNORED, 0);
}

bool LuaCSPlugin::Hook_FireEvent(IGameEvent* event, bool dont_broadcast) {
    IGameEventManager2* manager = META_IFACEPTR(IGameEventManager2);
    if (manager && manager != g_game_events) {
        g_game_events = manager;
        game_api_.set_game_event_manager(manager);
    }

    IGameEvent* copy = manager && event ? manager->DuplicateEvent(event) : nullptr;
    if (event_copy_overflow_depth_ != 0) {
        ++event_copy_overflow_depth_;
        if (copy && manager) manager->FreeEvent(copy);
        copy = nullptr;
    } else if (event_copy_count_ < event_copies_.size()) {
        event_copies_[event_copy_count_++] = copy;
    } else {
        event_copy_overflow_depth_ = 1;
        if (copy && manager) manager->FreeEvent(copy);
        copy = nullptr;
        write_console("[ERROR] (lua) Game-event nesting exceeded the bounded "
                      "LuaCS event-copy stack; Lua callbacks are skipped for "
                      "overflow events until nesting unwinds.");
    }

    if (!copy) RETURN_META_VALUE(MRES_IGNORED, true);

    const std::uint64_t token =
        game_api_.begin_event(copy, false, dont_broadcast);
    runtime_.dispatch_game_event(token, copy->GetName(), copy->GetID(),
                                 copy->IsReliable(), copy->IsLocal(), false,
                                 dont_broadcast);
    const auto decision = game_api_.end_event(token);
    if (decision.cancelled) {
        game_api_.free_event(event);
        RETURN_META_VALUE(MRES_SUPERCEDE, false);
    }
    if (decision.dont_broadcast != dont_broadcast) {
        RETURN_META_VALUE_NEWPARAMS(MRES_HANDLED, true,
                                    &IGameEventManager2::FireEvent,
                                    (event, decision.dont_broadcast));
    }
    RETURN_META_VALUE(MRES_IGNORED, true);
}

bool LuaCSPlugin::Hook_FireEventPost(IGameEvent*, bool dont_broadcast) {
    if (event_copy_overflow_depth_ != 0) {
        --event_copy_overflow_depth_;
        RETURN_META_VALUE(MRES_IGNORED, true);
    }
    if (event_copy_count_ == 0) RETURN_META_VALUE(MRES_IGNORED, true);

    IGameEvent* copy = event_copies_[--event_copy_count_];
    event_copies_[event_copy_count_] = nullptr;
    if (copy) {
        const std::uint64_t token =
            game_api_.begin_event(copy, true, dont_broadcast);
        runtime_.dispatch_game_event(token, copy->GetName(), copy->GetID(),
                                     copy->IsReliable(), copy->IsLocal(), true,
                                     dont_broadcast);
        game_api_.end_event(token);
        game_api_.free_event(copy);
    }
    RETURN_META_VALUE(MRES_IGNORED, true);
}

void LuaCSPlugin::remove_hooks() {
    if (game_frame_hook_id_ > 0) {
        SH_REMOVE_HOOK_ID(game_frame_hook_id_);
        game_frame_hook_id_ = -1;
    }
    if (client_active_hook_id_ > 0) {
        SH_REMOVE_HOOK_ID(client_active_hook_id_);
        client_active_hook_id_ = -1;
    }
    if (client_disconnect_hook_id_ > 0) {
        SH_REMOVE_HOOK_ID(client_disconnect_hook_id_);
        client_disconnect_hook_id_ = -1;
    }
    if (client_put_in_server_hook_id_ > 0) {
        SH_REMOVE_HOOK_ID(client_put_in_server_hook_id_);
        client_put_in_server_hook_id_ = -1;
    }
    if (client_connected_hook_id_ > 0) {
        SH_REMOVE_HOOK_ID(client_connected_hook_id_);
        client_connected_hook_id_ = -1;
    }
    if (client_command_hook_id_ > 0) {
        SH_REMOVE_HOOK_ID(client_command_hook_id_);
        client_command_hook_id_ = -1;
    }
    if (load_events_hook_id_ > 0) {
        SH_REMOVE_HOOK_ID(load_events_hook_id_);
        load_events_hook_id_ = -1;
    }
    if (fire_event_pre_hook_id_ > 0) {
        SH_REMOVE_HOOK_ID(fire_event_pre_hook_id_);
        fire_event_pre_hook_id_ = -1;
    }
    if (fire_event_post_hook_id_ > 0) {
        SH_REMOVE_HOOK_ID(fire_event_post_hook_id_);
        fire_event_post_hook_id_ = -1;
    }
}

void LuaCSPlugin::free_event_copies() {
    if (g_game_events) {
        while (event_copy_count_ != 0) {
            IGameEvent* copy = event_copies_[--event_copy_count_];
            event_copies_[event_copy_count_] = nullptr;
            if (copy) g_game_events->FreeEvent(copy);
        }
    } else {
        while (event_copy_count_ != 0) {
            event_copies_[--event_copy_count_] = nullptr;
        }
    }
    event_copy_overflow_depth_ = 0;
}
