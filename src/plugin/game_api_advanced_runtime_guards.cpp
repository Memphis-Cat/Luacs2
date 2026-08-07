// Final runtime validation boundary over the verified trace and schema-native
// grenade implementation. This layer does not change AdvancedWorld ABI v3; it
// rejects impossible/non-finite engine results before they cross into modules.
#include "game_api_advanced_schema_grenades_build.cpp"

namespace {

using TraceCallback = bool (*)(void*, const TraceRequest*, TraceResult*, char*,
                               std::size_t);
using GrenadeGetCallback = bool (*)(void*, int, GrenadeInfo*, char*,
                                    std::size_t);
using GrenadeAtCallback = bool (*)(void*, int, std::size_t, GrenadeInfo*, char*,
                                   std::size_t);
using GrenadeSpawnCallback = bool (*)(void*, const GrenadeSpawnRequest*,
                                      GrenadeInfo*, char*, std::size_t);

TraceCallback g_guarded_trace_base{};
GrenadeGetCallback g_guarded_grenade_get_base{};
GrenadeAtCallback g_guarded_grenade_at_base{};
GrenadeSpawnCallback g_guarded_grenade_spawn_base{};

bool guarded_finite_vector(const Vector3& value) {
    return std::isfinite(value.x) && std::isfinite(value.y) &&
           std::isfinite(value.z);
}

bool validate_trace_output(const TraceResult& value, std::string& error) {
    if (!value.valid) {
        error = "Source 2 returned a trace result marked invalid";
        return false;
    }
    if (value.shape < TraceShape::Line || value.shape > TraceShape::Mesh) {
        error = "Source 2 returned a trace shape outside the ABI range";
        return false;
    }
    if (!std::isfinite(value.fraction) || value.fraction < 0.0f ||
        value.fraction > 1.0f) {
        error = "Source 2 returned an invalid trace fraction";
        return false;
    }
    if (!std::isfinite(value.distance) || value.distance < 0.0f ||
        !std::isfinite(value.hit_offset) ||
        !std::isfinite(value.plane_distance)) {
        error = "Source 2 returned non-finite trace scalar data";
        return false;
    }
    if (!guarded_finite_vector(value.start) ||
        !guarded_finite_vector(value.end) ||
        !guarded_finite_vector(value.hit_position) ||
        !guarded_finite_vector(value.plane_normal)) {
        error = "Source 2 returned non-finite trace vector data";
        return false;
    }
    if (!value.hit && value.entity_index != -1) {
        error = "Source 2 trace miss unexpectedly retained a hit entity";
        return false;
    }
    if (value.entity_index < -1) {
        error = "Source 2 returned an invalid trace entity index";
        return false;
    }
    return true;
}

bool validate_grenade_output(void* context, const GrenadeInfo& value,
                             std::string& error) {
    if (!context) {
        error = "grenade validation context is null";
        return false;
    }
    if (!value.valid || value.entity_index < 0 ||
        value.type < GrenadeType::HighExplosive ||
        value.type > GrenadeType::Inferno) {
        error = "Source 2 returned invalid grenade identity data";
        return false;
    }
    auto* api = static_cast<LuaCSAdvancedApi*>(context);
    if (!api->entity(value.entity_index)) {
        error = "Source 2 returned a grenade entity that is no longer valid";
        return false;
    }
    if (!guarded_finite_vector(value.position) ||
        !guarded_finite_vector(value.velocity)) {
        error = "Source 2 returned non-finite grenade vector data";
        return false;
    }
    if (!std::isfinite(value.spawn_time) ||
        !std::isfinite(value.detonate_time) ||
        !std::isfinite(value.lifetime) ||
        !std::isfinite(value.smoke_effect_tick)) {
        error = "Source 2 returned non-finite grenade timing data";
        return false;
    }
    if (value.thrower_slot < -1 || value.thrower_slot >= 64 ||
        value.owner_entity_index < -1 || value.thrower_entity_index < -1) {
        error = "Source 2 returned invalid grenade ownership data";
        return false;
    }
    if (value.classname[0] == '\0') {
        error = "Source 2 returned a grenade without a classname";
        return false;
    }
    return true;
}

bool trace_runtime_guard(void* context, const TraceRequest* request,
                         TraceResult* output, char* error,
                         std::size_t error_size) {
    if (error && error_size) error[0] = '\0';
    if (!g_guarded_trace_base) {
        write_error(error, error_size, "verified trace callback is unavailable");
        return false;
    }
    if (!g_guarded_trace_base(context, request, output, error, error_size)) {
        return false;
    }
    std::string message;
    if (!output || !validate_trace_output(*output, message)) {
        write_error(error, error_size,
                    message.empty() ? "trace output is null" : message);
        return false;
    }
    return true;
}

bool grenade_get_runtime_guard(void* context, int entity_index,
                               GrenadeInfo* output, char* error,
                               std::size_t error_size) {
    if (error && error_size) error[0] = '\0';
    if (!g_guarded_grenade_get_base) {
        write_error(error, error_size, "schema grenade query callback is unavailable");
        return false;
    }
    if (!g_guarded_grenade_get_base(context, entity_index, output, error,
                                    error_size)) {
        return false;
    }
    std::string message;
    if (!output || !validate_grenade_output(context, *output, message)) {
        write_error(error, error_size,
                    message.empty() ? "grenade output is null" : message);
        return false;
    }
    return true;
}

bool grenade_at_runtime_guard(void* context, int type, std::size_t index,
                              GrenadeInfo* output, char* error,
                              std::size_t error_size) {
    if (error && error_size) error[0] = '\0';
    if (!g_guarded_grenade_at_base) {
        write_error(error, error_size,
                    "schema grenade enumeration callback is unavailable");
        return false;
    }
    if (!g_guarded_grenade_at_base(context, type, index, output, error,
                                   error_size)) {
        return false;
    }
    std::string message;
    if (!output || !validate_grenade_output(context, *output, message)) {
        write_error(error, error_size,
                    message.empty() ? "grenade output is null" : message);
        return false;
    }
    return true;
}

bool grenade_spawn_runtime_guard(void* context,
                                 const GrenadeSpawnRequest* request,
                                 GrenadeInfo* output, char* error,
                                 std::size_t error_size) {
    if (error && error_size) error[0] = '\0';
    if (!g_guarded_grenade_spawn_base) {
        write_error(error, error_size,
                    "schema grenade spawn callback is unavailable");
        return false;
    }
    if (!g_guarded_grenade_spawn_base(context, request, output, error,
                                      error_size)) {
        return false;
    }
    std::string message;
    if (!output || !validate_grenade_output(context, *output, message)) {
        write_error(error, error_size,
                    message.empty() ? "spawned grenade output is null" : message);
        return false;
    }
    return true;
}

struct AdvancedRuntimeGuardRegistration {
    AdvancedRuntimeGuardRegistration() {
        auto& services = g_advanced_api.services;
        g_guarded_trace_base = services.trace;
        g_guarded_grenade_get_base = services.grenade_get;
        g_guarded_grenade_at_base = services.grenade_at;
        g_guarded_grenade_spawn_base = services.grenade_spawn;
        services.trace = &trace_runtime_guard;
        services.grenade_get = &grenade_get_runtime_guard;
        services.grenade_at = &grenade_at_runtime_guard;
        services.grenade_spawn = &grenade_spawn_runtime_guard;
    }
};

AdvancedRuntimeGuardRegistration g_advanced_runtime_guard_registration;

} // namespace
