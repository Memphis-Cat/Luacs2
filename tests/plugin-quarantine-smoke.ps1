$ErrorActionPreference = "Stop"

$root = (Resolve-Path "$PSScriptRoot\..").Path
$runtimeLua = Join-Path $root "src\runtime\runtime_lua.cpp"
$runtimeHelpers = Join-Path $root "src\runtime\runtime_helpers.h"
$cmake = Join-Path $root "CMakeLists.txt"
$generatedInjection = Join-Path $root "tools\inject-generated-game-api.cmake"

foreach ($path in @($runtimeLua, $runtimeHelpers, $cmake, $generatedInjection)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "required quarantine test source is missing: $path"
    }
}

$luaText = [System.IO.File]::ReadAllText($runtimeLua)
$helperText = [System.IO.File]::ReadAllText($runtimeHelpers)
$cmakeText = [System.IO.File]::ReadAllText($cmake)
$injectionText = [System.IO.File]::ReadAllText($generatedInjection)

foreach ($token in @(
    "loaded.get() == &vm",
    "disable_vm(vm.state",
    "vm.events.clear();",
    "vm.commands.clear();",
    "vm.timers.clear();",
    "luaL_unref(vm.state, LUA_REGISTRYINDEX, callback.reference)",
    "luaL_unref(vm.state, LUA_REGISTRYINDEX, timer.reference)",
    "Plugin disabled after an uncaught Lua callback error",
    "other plugins remain active"
)) {
    if (-not $luaText.Contains($token)) {
        throw "runtime quarantine implementation is missing '$token'"
    }
}

foreach ($token in @(
    'kDisabledRegistryKey = "LuaCS.Disabled"',
    'kDisableReasonRegistryKey = "LuaCS.DisableReason"',
    "inline bool vm_disabled(lua_State* state)",
    "inline void disable_vm(lua_State* state"
)) {
    if (-not $helperText.Contains($token)) {
        throw "runtime quarantine registry support is missing '$token'"
    }
}

if (-not $injectionText.Contains("sourcehook_impl_chookmaninfo.cpp")) {
    throw "generated-source injection lost required sourcehook_impl_chookmaninfo.cpp"
}
if (-not $cmakeText.Contains("/WX")) {
    throw "CMake no longer treats MSVC warnings as errors"
}

Write-Host (
    "LuaCS plugin quarantine tests passed: uncaught callbacks disable only the " +
    "loaded VM, event/command/timer registry references are released, the " +
    "plugin cannot keep executing, SourceHook remains complete, and /WX is enabled.")
