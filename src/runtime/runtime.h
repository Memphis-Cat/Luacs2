#pragma once

#include "luacs/module_api.h"

#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

extern "C" {
#include "lauxlib.h"
#include "lualib.h"
}

namespace luacs {

inline constexpr std::string_view kLuaCSVersion = "BETA TESTING 1.0";

struct PlayerSnapshot {
    int slot{-1};
    std::string name;
    std::uint64_t steam64{0};
    std::string steam_id;
    bool fake{false};
    bool connected{false};
    bool active{false};
};

struct HostOperations {
    std::function<bool(std::uint64_t, std::string_view)> event_has_key;
    std::function<bool(std::uint64_t, std::string_view)> event_is_empty;
    std::function<bool(std::uint64_t, std::string_view, bool, bool&)> event_get_bool;
    std::function<bool(std::uint64_t, std::string_view, int, int&)> event_get_int;
    std::function<bool(std::uint64_t, std::string_view, std::uint64_t,
                       std::uint64_t&)> event_get_uint64;
    std::function<bool(std::uint64_t, std::string_view, float, float&)> event_get_float;
    std::function<bool(std::uint64_t, std::string_view, std::string_view,
                       std::string&)> event_get_string;
    std::function<bool(std::uint64_t, std::string_view, int&)> event_get_player_slot;
    std::function<bool(std::uint64_t, std::string_view, int&)> event_get_entity_index;
    std::function<bool(std::uint64_t, std::string_view, int&)> event_get_pawn_index;
    std::function<bool(std::uint64_t, std::string_view, bool)> event_set_bool;
    std::function<bool(std::uint64_t, std::string_view, int)> event_set_int;
    std::function<bool(std::uint64_t, std::string_view, std::uint64_t)> event_set_uint64;
    std::function<bool(std::uint64_t, std::string_view, float)> event_set_float;
    std::function<bool(std::uint64_t, std::string_view, std::string_view)> event_set_string;
    std::function<bool(std::uint64_t)> event_cancel;
    std::function<bool(std::uint64_t, bool)> event_set_dont_broadcast;

    std::function<bool(int, PlayerState&, std::string&)> player_state;
    std::function<bool(int, PlayerIntField, int, std::string&)> player_set_int;
    std::function<bool(int, PlayerBoolField, bool, std::string&)> player_set_bool;
    std::function<bool(int, const Vector3*, const Vector3*, const Vector3*,
                       std::string&)> player_teleport;
    std::function<bool(int, bool, std::string&)> player_kill;
    std::function<bool(int, std::string&)> player_respawn;
    std::function<bool(int, int, bool, std::string&)> player_change_team;

    std::function<bool(int, int, std::string_view, std::string&)> hud_print;
    std::function<bool(std::string_view)> cvar_exists;
    std::function<bool(std::string_view, std::string&, std::string&)> cvar_get;
    std::function<bool(std::string_view, std::string_view, std::string&)> cvar_set;

    std::function<bool(int, std::string_view, WeaponInfo&, std::string&)> weapon_give;
    std::function<bool(int, std::string&)> weapon_remove_all;
    std::function<bool(int, std::string&)> weapon_drop_active;
    std::function<std::size_t(int, std::string&)> weapon_count;
    std::function<bool(int, std::size_t, WeaponInfo&, std::string&)> weapon_at;
    std::function<bool(int, WeaponInfo&, std::string&)> weapon_get;
    std::function<bool(int, int, bool, std::string&)> weapon_remove;
    std::function<bool(int, int, std::string&)> weapon_drop;
    std::function<bool(int, int, std::string&)> weapon_switch;
    std::function<bool(int, int, int, std::string&)> weapon_set_clip;
    std::function<bool(int, int, int, std::string&)> weapon_set_reserve;
    std::function<bool(int, int, int&, std::string&)> weapon_get_ammo;
    std::function<bool(int, int, int, std::string&)> weapon_set_ammo;
};

class Runtime {
public:
    using ConsoleWriter = std::function<void(std::string_view)>;
    using ServerCommand = std::function<void(std::string_view)>;

    Runtime();
    ~Runtime();

    Runtime(const Runtime&) = delete;
    Runtime& operator=(const Runtime&) = delete;

    bool initialize(std::filesystem::path root, ConsoleWriter console_writer,
                    ServerCommand server_command, std::string& error);
    void set_host_operations(HostOperations host_operations);
    void shutdown();
    void load_plugins();
    void tick();

    void level_init(std::string_view map_name);
    void level_shutdown();
    void player_connected(int slot, std::string_view name, std::uint64_t steam64,
                          std::string_view steam_id, bool fake);
    void player_active(int slot, std::string_view name, std::uint64_t steam64);
    void player_put_in_server(int slot, std::string_view name, std::uint64_t steam64);
    void player_disconnected(int slot, std::string_view name, std::uint64_t steam64,
                             std::string_view steam_id);
    void client_command(int slot, std::string_view command_line);

    void dispatch_game_event(std::uint64_t token, std::string_view name, int id,
                             bool reliable, bool local, bool post,
                             bool dont_broadcast);
    double now() const;

private:
    struct EventCallback {
        std::uint64_t subscription_id{};
        int reference{LUA_NOREF};
        EventCallbackMode mode{EventCallbackMode::EventTable};
        bool post{false};
    };

    struct Timer {
        std::uint64_t id{};
        int reference{LUA_NOREF};
        double due{};
        double interval{};
        bool repeat{};
        bool cancelled{};
    };

    struct PluginMetadata {
        std::string name;
        std::string author;
        std::string version;
        std::string description;
    };

    struct ScriptVm {
        Runtime* runtime{};
        std::filesystem::path source_path;
        std::string name;
        std::string author;
        std::string version;
        std::string description;
        lua_State* state{};
        std::filesystem::path log_path;
        std::unordered_map<std::string, std::vector<EventCallback>> events;
        std::unordered_map<std::string, std::vector<int>> commands;
        std::vector<Timer> timers;
        std::vector<void*> module_handles;

        ~ScriptVm();
    };

    std::filesystem::path root_;
    std::filesystem::path bin_dir_;
    std::filesystem::path plugins_dir_;
    std::filesystem::path config_dir_;
    std::filesystem::path logs_dir_;
    std::filesystem::path core_log_path_;
    std::array<std::uint8_t, 32> key_{};
    ConsoleWriter console_writer_;
    ServerCommand server_command_;
    HostOperations host_operations_;
    std::chrono::steady_clock::time_point started_{};
    std::vector<std::unique_ptr<ScriptVm>> scripts_;
    std::unordered_map<lua_State*, ScriptVm*> state_map_;
    std::unordered_map<int, PlayerSnapshot> players_;
    std::unordered_map<std::string, PluginMetadata> plugin_metadata_cache_;
    std::unordered_map<std::string, std::string> plugin_failures_;
    std::uint64_t next_timer_id_{1};
    std::uint64_t next_event_subscription_id_{1};
    Services services_{};

    bool load_plugin(const std::filesystem::path& path);
    void read_plugin_metadata(ScriptVm& vm);
    void emit(std::string_view event_name,
              const std::function<void(lua_State*)>& push_event_table);
    void emit_player(std::string_view event_name, const PlayerSnapshot& player);
    void dispatch_command(const PlayerSnapshot* player, std::string_view name,
                          std::string_view arguments, std::string_view raw);
    void log(ScriptVm& vm, std::string_view text);
    void log_runtime(std::string_view text);
    std::filesystem::path create_log_path() const;

    ScriptVm* find_vm(lua_State* state);
    const ScriptVm* find_vm(lua_State* state) const;
    static Runtime* from_services(void* context);
    const PlayerSnapshot* player_snapshot(int slot) const;

    static void service_log(void* context, lua_State* state, const char* text);
    static double service_now(void* context);
    static std::uint64_t service_event_on(void* context, lua_State* state,
                                          const char* event_name,
                                          int callback_index,
                                          EventCallbackMode mode, bool post);
    static bool service_event_off(void* context, lua_State* state,
                                  std::uint64_t subscription_id);
    static bool service_event_has_key(void*, std::uint64_t, const char*);
    static bool service_event_is_empty(void*, std::uint64_t, const char*);
    static bool service_event_get_bool(void*, std::uint64_t, const char*, bool,
                                       bool*);
    static bool service_event_get_int(void*, std::uint64_t, const char*, int,
                                      int*);
    static bool service_event_get_uint64(void*, std::uint64_t, const char*,
                                         std::uint64_t, std::uint64_t*);
    static bool service_event_get_float(void*, std::uint64_t, const char*, float,
                                        float*);
    static bool service_event_get_string(void*, std::uint64_t, const char*,
                                         const char*, char*, std::size_t);
    static bool service_event_get_player_slot(void*, std::uint64_t, const char*,
                                              int*);
    static bool service_event_get_entity_index(void*, std::uint64_t, const char*,
                                               int*);
    static bool service_event_get_pawn_index(void*, std::uint64_t, const char*,
                                             int*);
    static bool service_event_set_bool(void*, std::uint64_t, const char*, bool);
    static bool service_event_set_int(void*, std::uint64_t, const char*, int);
    static bool service_event_set_uint64(void*, std::uint64_t, const char*,
                                         std::uint64_t);
    static bool service_event_set_float(void*, std::uint64_t, const char*, float);
    static bool service_event_set_string(void*, std::uint64_t, const char*,
                                         const char*);
    static bool service_event_cancel(void*, std::uint64_t);
    static bool service_event_set_dont_broadcast(void*, std::uint64_t, bool);

    static std::uint64_t service_timer_add(void*, lua_State*, double, bool, int);
    static bool service_timer_cancel(void*, lua_State*, std::uint64_t);
    static bool service_player_get(void*, int, PlayerInfo*);
    static std::size_t service_player_count(void*);
    static bool service_player_at(void*, std::size_t, PlayerInfo*);
    static bool service_player_state(void*, int, PlayerState*, char*, std::size_t);
    static bool service_player_set_int(void*, int, PlayerIntField, int, char*,
                                       std::size_t);
    static bool service_player_set_bool(void*, int, PlayerBoolField, bool, char*,
                                        std::size_t);
    static bool service_player_teleport(void*, int, const Vector3*,
                                        const Vector3*, const Vector3*, char*,
                                        std::size_t);
    static bool service_player_kill(void*, int, bool, char*, std::size_t);
    static bool service_player_respawn(void*, int, char*, std::size_t);
    static bool service_player_change_team(void*, int, int, bool, char*,
                                           std::size_t);
    static bool service_command_on(void*, lua_State*, const char*, int);
    static bool service_hud_print(void*, int, int, const char*, char*,
                                  std::size_t);
    static bool service_cvar_exists(void*, const char*);
    static bool service_cvar_get(void*, const char*, char*, std::size_t, char*,
                                 std::size_t);
    static bool service_cvar_set(void*, const char*, const char*, char*,
                                 std::size_t);
    static bool service_weapon_give(void*, int, const char*, WeaponInfo*, char*,
                                    std::size_t);
    static bool service_weapon_remove_all(void*, int, char*, std::size_t);
    static bool service_weapon_drop_active(void*, int, char*, std::size_t);
    static std::size_t service_weapon_count(void*, int, char*, std::size_t);
    static bool service_weapon_at(void*, int, std::size_t, WeaponInfo*, char*,
                                  std::size_t);
    static bool service_weapon_get(void*, int, WeaponInfo*, char*, std::size_t);
    static bool service_weapon_remove(void*, int, int, bool, char*, std::size_t);
    static bool service_weapon_drop(void*, int, int, char*, std::size_t);
    static bool service_weapon_switch(void*, int, int, char*, std::size_t);
    static bool service_weapon_set_clip(void*, int, int, int, char*, std::size_t);
    static bool service_weapon_set_reserve(void*, int, int, int, char*,
                                           std::size_t);
    static bool service_weapon_get_ammo(void*, int, int, int*, char*,
                                        std::size_t);
    static bool service_weapon_set_ammo(void*, int, int, int, char*,
                                        std::size_t);

    static int lua_print(lua_State* state);
    static int lua_vector(lua_State* state);
    static int lua_color(lua_State* state);
    static int lua_value_tostring(lua_State* state);
    static int lua_module_searcher(lua_State* state);
    static int lua_module_loader(lua_State* state);
    static int lua_native_open_trampoline(lua_State* state);
    static int lua_cs2_root_loader(lua_State* state);
    static int lua_cs2_root_index(lua_State* state);
    static int lua_traceback(lua_State* state);

    bool install_core(ScriptVm& vm);
    bool protected_call(ScriptVm& vm, int argument_count, int result_count,
                        std::string_view context);
    void push_player(lua_State* state, const PlayerSnapshot& player) const;
    static std::string normalize_name(std::string_view name);
    static std::pair<std::string, std::string>
    parse_command(std::string_view command_line);
};

} // namespace luacs
