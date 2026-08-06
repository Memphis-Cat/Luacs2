// Final validation layer over the complete ABI implementation. This file is
// intentionally the only advanced adapter selected by CMake.
#include "game_api_advanced_final_build.cpp"

namespace {

// The original resolver stored the native function behind an opaque pointer
// parameter. Prove at compile time that the pinned SDK layout still matches
// that compatibility boundary; the actual ray is always constructed as Ray_t.
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

    return trace_complete(context, request, output, error, error_size);
}

struct VerifiedAdvancedWorldRegistration {
    VerifiedAdvancedWorldRegistration() {
        g_advanced_api.services.trace = &trace_verified;
    }
};

VerifiedAdvancedWorldRegistration g_verified_advanced_world_registration;

} // namespace
