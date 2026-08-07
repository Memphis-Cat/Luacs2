plugin = {
    name = "LuaCS API Test Disconnect Race",
    author = "Memphis-Cat",
    version = "1.0.0",
    description = "Verifies delayed use of a disconnected player fails safely instead of crashing CS2."
}

local events = require("cs2.events")
local timers = require("cs2.timers")

local pending = 0

events.Instance:OnPlayerDisconnect(function(player)
    if not player then
        print("[LuaTestDisconnectRace] SKIP disconnect event had no player object")
        return
    end

    pending = pending + 1
    local captured = player
    local captured_name = tostring(player.name or "unknown")
    local captured_slot = tostring(player.slot or "?")
    print("[LuaTestDisconnectRace] captured disconnect name=" .. captured_name ..
        " slot=" .. captured_slot .. "; checking again in 3 seconds")

    timers.after(3.0, function()
        pending = pending - 1
        local call_ok, refreshed, error_message = pcall(function()
            return captured:refresh()
        end)

        if not call_ok then
            print("[LuaTestDisconnectRace] PASS stale player raised a Lua-level error: " ..
                tostring(refreshed))
            return
        end
        if not refreshed then
            print("[LuaTestDisconnectRace] PASS stale player refresh failed safely: " ..
                tostring(error_message or "invalid player"))
            return
        end
        if captured.valid == false or captured.connected == false then
            print("[LuaTestDisconnectRace] PASS stale player remained non-live after refresh")
            return
        end

        print("[LuaTestDisconnectRace] FAIL disconnected player unexpectedly refreshed as live")
    end)
end)

function plugin:unload()
    print("[LuaTestDisconnectRace] unload pending=" .. tostring(pending))
end

print("[LuaTestDisconnectRace] loaded. Disconnect a player and wait at least 3 seconds.")
