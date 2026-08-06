#pragma once

#include <filesystem>
#include <string>

bool LuaCSBindGameServerModule(void* server_interface, std::string& error);
void* LuaCSGameServerModule();
const std::filesystem::path& LuaCSGameServerModulePath();
