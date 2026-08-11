#include "game_api_internal.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <igameevents.h>
#include <playerslot.h>
#include <tier0/dbg.h>
#include <tier1/convar.h>

#include <algorithm>
#include <cstdlib>
#include <string>

extern IGameEventManager2* g_game_events;

namespace {

using LegacyGameEventListenerFn = IGameEventListener2*(__fastcall*)(CPlayerSlot);

LegacyGameEventListenerFn resolve_legacy_game_event_listener() {
    static LegacyGameEventListenerFn function = [] {
        const HMODULE server = GetModuleHandleW(L"server.dll");
        if (!server) return static_cast<LegacyGameEventListenerFn>(nullptr);
        constexpr std::string_view pattern =
            "48 8B 15 ? ? ? ? 48 85 D2 74 ? 85 C9";
        return reinterpret_cast<LegacyGameEventListenerFn>(
            luacs_game_internal::find_pattern(server, pattern));
    }();
    return function;
}

bool parse_integer(const char* text, int minimum, int maximum, int& output) {
    if (!text || !*text) return false;
    char* end = nullptr;
    const long value = std::strtol(text, &end, 10);
    if (!end || *end != '\0' || value < minimum || value > maximum) {
        return false;
    }
    output = static_cast<int>(value);
    return true;
}

void command_center_html(const CCommandContext&, const CCommand& command) {
    if (command.ArgC() < 4) {
        Warning("LuaCS: usage: luacs_centerhtml <slot> <duration> <html>\n");
        return;
    }

    int slot{};
    int duration{};
    if (!parse_integer(command.Arg(1), 0, 63, slot) ||
        !parse_integer(command.Arg(2), 1, 30, duration)) {
        Warning("LuaCS: luacs_centerhtml received an invalid slot or duration.\n");
        return;
    }

    std::string html;
    for (int index = 3; index < command.ArgC(); ++index) {
        if (index != 3) html.push_back(' ');
        if (const char* argument = command.Arg(index)) html += argument;
    }
    if (html.empty() || html.size() > 1000 || html.find('\0') != std::string::npos) {
        Warning("LuaCS: luacs_centerhtml HTML must contain 1-1000 bytes.\n");
        return;
    }

    if (!g_game_events) {
        Warning("LuaCS: game event manager is not ready for center HTML.\n");
        return;
    }

    const auto legacy_listener = resolve_legacy_game_event_listener();
    if (!legacy_listener) {
        Warning("LuaCS: LegacyGameEventListener signature is unavailable.\n");
        return;
    }

    IGameEventListener2* listener = legacy_listener(CPlayerSlot(slot));
    if (!listener) return;

    IGameEvent* event =
        g_game_events->CreateEvent("show_survival_respawn_status", true);
    if (!event) return;

    event->SetInt("duration", duration);
    event->SetString("loc_token", html.c_str());
    event->SetPlayer("userid", CPlayerSlot(slot));
    listener->FireGameEvent(event);
    g_game_events->FreeEvent(event);
}

ConCommand g_luacs_center_html_command(
    "luacs_centerhtml", ConCommandCallbackInfo_t(&command_center_html),
    "Internal LuaCS center-HTML bridge. Usage: luacs_centerhtml <slot> <duration> <html>.",
    FCVAR_RELEASE);

} // namespace
