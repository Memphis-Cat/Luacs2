// Final validation layer over the complete ABI implementation. This file is
// intentionally part of the generated advanced adapter selected by CMake.
#include "game_api_advanced_final_build.cpp"

namespace {

// The generated resolver replaces the obsolete 44-byte placeholder with the
// pinned SDK type. These assertions prevent a future source/configuration drift
// from silently restoring a different ABI.
static_assert(sizeof(Ray_t) == sizeof(LuaCSAdvancedApi::NativeRay),
              "pinned Source 2 Ray_t layout changed");
static_assert(alignof(Ray_t) == alignof(LuaCSAdvancedApi::NativeRay),
              "pinned Source 2 Ray_t alignment changed");

bool trace_verified(void* context, const TraceRequest* request,
                    TraceResult* output, char* error,
                    std::size_t error_size) {
    if (error && error_size) error[0] = '\0';
    if (!context || !request || !output) {
        write_error(error, error_size,
                    "trace context, request, or output is null");
        return false;
    }

    constexpr std::uint64_t valid_object_sets =
        static_cast<std::uint64_t>(RNQUERY_OBJECTS_ALL);
    if ((request->ignore_entities_mask & ~valid_object_sets) != 0) {
        write_error(error, error_size,
                    "trace object_set_mask contains unsupported Source 2 bits");
        return false;
    }

    if (!trace_complete(context, request, output, error, error_size)) {
        return false;
    }

    // A miss has no collision point or plane. Report the requested end point
    // and full travelled distance instead of trusting hit-only fields that an
    // engine implementation may leave untouched.
    if (!output->hit) {
        output->hit_position = output->end;
        output->plane_normal = {};
        output->plane_distance = 0.0f;
        output->hit_offset = 0.0f;
        output->triangle = -1;
        output->bone = -1;
        output->hitbox = -1;
        output->hitgroup = 0;
        output->entity_index = -1;
        output->entity_handle = 0xFFFFFFFFu;
        output->surface_flags = 0;
        output->surface_name[0] = '\0';
        const float delta_x = output->end.x - output->start.x;
        const float delta_y = output->end.y - output->start.y;
        const float delta_z = output->end.z - output->start.z;
        output->distance =
            std::sqrt(delta_x * delta_x + delta_y * delta_y +
                      delta_z * delta_z);
    }
    return true;
}

struct VerifiedAdvancedWorldRegistration {
    VerifiedAdvancedWorldRegistration() {
        g_advanced_api.services.trace = &trace_verified;
    }
};

VerifiedAdvancedWorldRegistration g_verified_advanced_world_registration;

} // namespace
