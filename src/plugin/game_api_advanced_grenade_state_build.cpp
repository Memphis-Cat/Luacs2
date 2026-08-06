// Add subclass-specific grenade state on top of the schema-native lifecycle.
#include "game_api_advanced_schema_grenades_build.cpp"

namespace {

bool grenade_get_state_impl(LuaCSAdvancedApi* api, int entity_index,
                            GrenadeInfo& output, std::string& error) {
    if (!grenade_get_schema_impl(api, entity_index, output, error)) {
        return false;
    }

    PropertyValue value;
    if ((output.type == GrenadeType::Molotov ||
         output.type == GrenadeType::Incendiary) &&
        api->optional_value(entity_index, "m_bDetonated", value)) {
        output.exploded = property_bool(value, output.exploded);
    }
    if (output.type == GrenadeType::Inferno &&
        api->optional_value(entity_index, "m_nFireLifetime", value)) {
        output.lifetime = property_float(value, output.lifetime);
    }
    return true;
}

bool grenade_get_state(void* context, int entity_index,
                       GrenadeInfo* output, char* error,
                       std::size_t error_size) {
    if (error && error_size) error[0] = '\0';
    if (!context || !output) {
        write_error(error, error_size,
                    "grenade context or output is null");
        return false;
    }

    std::string message;
    const bool result = grenade_get_state_impl(
        static_cast<LuaCSAdvancedApi*>(context), entity_index, *output,
        message);
    if (!result) write_error(error, error_size, message);
    return result;
}

bool grenade_at_state(void* context, int type, std::size_t index,
                      GrenadeInfo* output, char* error,
                      std::size_t error_size) {
    if (error && error_size) error[0] = '\0';
    if (!context || !output) {
        write_error(error, error_size,
                    "grenade context or output is null");
        return false;
    }

    auto* api = static_cast<LuaCSAdvancedApi*>(context);
    std::string message;
    const auto values = final_grenade_list(api, type, message);
    if (!message.empty()) {
        write_error(error, error_size, message);
        return false;
    }
    if (index >= values.size()) {
        write_error(error, error_size,
                    "grenade result index is out of range");
        return false;
    }
    if (!grenade_get_state_impl(api, values[index], *output, message)) {
        write_error(error, error_size, message);
        return false;
    }
    return true;
}

struct GrenadeStateRegistration {
    GrenadeStateRegistration() {
        auto& services = g_advanced_api.services;
        services.grenade_get = &grenade_get_state;
        services.grenade_at = &grenade_at_state;
    }
};

GrenadeStateRegistration g_grenade_state_registration;

} // namespace
