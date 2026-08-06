#pragma once

#include "game_api.h"
#include "runtime.h"

#include <ISmmPlugin.h>
#include <iserver.h>

#include <vector>

class IGameEvent;
class IGameEventManager2;

class LuaCSPlugin final : public ISmmPlugin, public IMetamodListener {
public:
    bool Load(PluginId id, ISmmAPI* ismm, char* error, size_t maxlen,
              bool late) override;
    bool Unload(char* error, size_t maxlen) override;
    void AllPluginsLoaded() override;

    void OnLevelInit(const char* map_name, const char* map_entities,
                     const char* old_level, const char* landmark_name,
                     bool load_game, bool background);
    void OnLevelShutdown();

    void Hook_GameFrame(bool simulating, bool first_tick, bool last_tick);
    void Hook_ClientActive(CPlayerSlot slot, bool load_game, const char* name,
                           uint64 xuid);
    void Hook_ClientDisconnect(CPlayerSlot slot,
                               ENetworkDisconnectionReason reason,
                               const char* name, uint64 xuid,
                               const char* network_id);
    void Hook_ClientPutInServer(CPlayerSlot slot, const char* name, int type,
                                uint64 xuid);
    void Hook_OnClientConnected(CPlayerSlot slot, const char* name, uint64 xuid,
                                const char* network_id, const char* address,
                                bool fake_player);
    void Hook_ClientCommand(CPlayerSlot slot, const CCommand& command);

    bool Hook_FireEvent(IGameEvent* event, bool dont_broadcast);
    bool Hook_FireEventPost(IGameEvent* event, bool dont_broadcast);

    const char* GetAuthor() override {
        return "Memphis-Cat / LuaCS contributors";
    }
    const char* GetName() override { return "LuaCS"; }
    const char* GetDescription() override {
        return "Modular Lua 5.5 runtime for CS2 Metamod";
    }
    const char* GetURL() override {
        return "https://github.com/Memphis-Cat/Luacs2";
    }
    const char* GetLicense() override { return "MIT"; }
    const char* GetVersion() override {
        return "0.4.0-teams-entities-sounds";
    }
    const char* GetDate() override { return __DATE__; }
    const char* GetLogTag() override { return "LUACS"; }

private:
    void free_event_copies();

    LuaCSGameApi game_api_;
    luacs::Runtime runtime_;
    int fire_event_pre_hook_id_{-1};
    int fire_event_post_hook_id_{-1};
    std::vector<IGameEvent*> event_copies_;
};

extern LuaCSPlugin g_LuaCSPlugin;
PLUGIN_GLOBALVARS();
