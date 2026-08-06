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

struct PlayerSnapshot {
    int slot{-1};
    std::string name;
    std::uint64_t steam64{0};
    std::string steam_id;
    bool fake{false};
    bool connected{false};
    bool active{false};
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

    double now() const;

private:
    struct EventCallback {
        int reference{LUA_NOREF};
        EventCallbackMode mode{EventCallbackMode::EventTable};
    };

    struct Timer {
        std::uint64_t id{};
        int reference{LUA_NOREF};
        double due{};
        double interval{};
        bool repeat{};
        bool cancelled{};
    };

    struct ScriptVm {
        Runtime* runtime{};
        std::filesystem::path source_path;
        std::string name;
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
    std::chrono::steady_clock::time_point started_{};
    std::vector<std::unique_ptr<ScriptVm>> scripts_;
    std::unordered_map<lua_State*, ScriptVm*> state_map_;
    std::unordered_map<int, PlayerSnapshot> players_;
    std::uint64_t next_timer_id_{1};
    Services services_{};

    bool load_plugin(const std::filesystem::path& path);
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

    static void service_log(void* context, lua_State* state, const char* text);
    static double service_now(void* context);
    static bool service_event_on(void* context, lua_State* state, const char* event_name,
                                 int callback_index, EventCallbackMode mode);
    static std::uint64_t service_timer_add(void* context, lua_State* state, double delay_seconds,
                                           bool repeat, int callback_index);
    static bool service_timer_cancel(void* context, lua_State* state, std::uint64_t timer_id);
    static bool service_player_get(void* context, int slot, PlayerInfo* output);
    static std::size_t service_player_count(void* context);
    static bool service_player_at(void* context, std::size_t index, PlayerInfo* output);
    static bool service_command_on(void* context, lua_State* state, const char* command_name,
                                   int callback_index);

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
    static std::pair<std::string, std::string> parse_command(std::string_view command_line);
};

} // namespace luacs
