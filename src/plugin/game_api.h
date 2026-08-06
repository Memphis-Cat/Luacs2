#pragma once

#include "runtime.h"

#include <filesystem>
#include <memory>
#include <string>

class IGameEvent;
class IGameEventManager2;

struct LuaCSEventDecision {
    bool cancelled{false};
    bool dont_broadcast{false};
};

class LuaCSGameApi {
public:
    LuaCSGameApi();
    ~LuaCSGameApi();

    LuaCSGameApi(const LuaCSGameApi&) = delete;
    LuaCSGameApi& operator=(const LuaCSGameApi&) = delete;

    bool initialize(const std::filesystem::path& luacs_root, std::string& error);
    void shutdown();
    luacs::HostOperations host_operations();

    IGameEventManager2* event_manager() const;
    void set_event_manager(IGameEventManager2* manager);

    std::uint64_t begin_event(IGameEvent* event, bool post,
                              bool dont_broadcast);
    LuaCSEventDecision end_event(std::uint64_t token);

    void* game_event_manager_init_address() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
