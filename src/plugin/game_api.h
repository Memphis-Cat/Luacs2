#pragma once

#include "runtime.h"

#include <entity2/entityinstance.h>

#include <filesystem>
#include <memory>
#include <string>

class LuaCSGameApi {
public:
    LuaCSGameApi();
    ~LuaCSGameApi();

    LuaCSGameApi(const LuaCSGameApi&) = delete;
    LuaCSGameApi& operator=(const LuaCSGameApi&) = delete;

    bool initialize(const std::filesystem::path& luacs_root, std::string& error);
    void shutdown();
    luacs::HostOperations host_operations();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
