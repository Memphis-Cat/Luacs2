cmake_minimum_required(VERSION 3.26)
project(LuaCS VERSION 0.1.0 LANGUAGES C CXX)

if(NOT WIN32 OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "LuaCS currently targets 64-bit Windows CS2 servers only.")
endif()
if(NOT MSVC)
    message(FATAL_ERROR "LuaCS and the CS2 HL2SDK require the 64-bit Microsoft Visual C++ compiler.")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

add_compile_options(/utf-8 /MP /wd4005)
add_compile_definitions(
    NOMINMAX
    WIN32_LEAN_AND_MEAN
    COMPILER_MSVC
    COMPILER_MSVC64
    _WIN32
    _WINDOWS
    _ALLOW_KEYWORD_MACROS
    __STDC_LIMIT_MACROS
    _CRT_SECURE_NO_WARNINGS=1
    _CRT_SECURE_NO_DEPRECATE=1
    _CRT_NONSTDC_NO_DEPRECATE=1
    _ITERATOR_DEBUG_LEVEL=0)

set(LUACS_PACKAGE_ROOT "${CMAKE_BINARY_DIR}/package/game/csgo/addons")
set(LUACS_ROOT "${LUACS_PACKAGE_ROOT}/LuaCS")
set(LUACS_BIN "${LUACS_ROOT}/bin")
set(LUACS_SCRIPTING "${LUACS_ROOT}/scripting")

function(luacs_runtime_output target directory)
    set_target_properties(${target} PROPERTIES
        PREFIX ""
        RUNTIME_OUTPUT_DIRECTORY "${directory}"
        LIBRARY_OUTPUT_DIRECTORY "${directory}"
        ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
        RUNTIME_OUTPUT_DIRECTORY_DEBUG "${directory}"
        RUNTIME_OUTPUT_DIRECTORY_RELEASE "${directory}"
        RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${directory}"
        RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${directory}")
endfunction()

file(GLOB LUA_SOURCES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/thirdparty/lua-5.5.1/src/*.c")
list(FILTER LUA_SOURCES EXCLUDE REGEX "/(lua|luac|onelua)\\.c$")
add_library(lua55 SHARED ${LUA_SOURCES})
target_include_directories(lua55 PUBLIC "${CMAKE_SOURCE_DIR}/thirdparty/lua-5.5.1/src")
target_compile_definitions(lua55 PRIVATE LUA_BUILD_AS_DLL)
target_compile_definitions(lua55 INTERFACE LUA_BUILD_AS_DLL)
luacs_runtime_output(lua55 "${LUACS_BIN}")

add_library(lua55_static STATIC ${LUA_SOURCES})
target_include_directories(lua55_static PUBLIC "${CMAKE_SOURCE_DIR}/thirdparty/lua-5.5.1/src")
set_target_properties(lua55_static PROPERTIES OUTPUT_NAME "lua55-static")

add_library(luacs_common STATIC
    src/common/smg.cpp
    src/common/smg.h)
target_include_directories(luacs_common PUBLIC src/common)
target_link_libraries(luacs_common PUBLIC bcrypt)

add_executable(luacs_compile src/compiler/main.cpp)
target_link_libraries(luacs_compile PRIVATE lua55_static luacs_common)
target_include_directories(luacs_compile PRIVATE thirdparty/lua-5.5.1/src src/common)
set_target_properties(luacs_compile PROPERTIES OUTPUT_NAME "compile")
luacs_runtime_output(luacs_compile "${LUACS_SCRIPTING}")

function(add_luacs_module module source)
    add_library(${module} SHARED ${source})
    target_include_directories(${module} PRIVATE include thirdparty/lua-5.5.1/src)
    target_link_libraries(${module} PRIVATE lua55)
    set_target_properties(${module} PROPERTIES OUTPUT_NAME "${module}")
    luacs_runtime_output(${module} "${LUACS_BIN}")
endfunction()

add_luacs_module(events src/modules/events/events.cpp)
add_luacs_module(timers src/modules/timers/timers.cpp)
add_luacs_module(players src/modules/players/players.cpp)
add_luacs_module(commands src/modules/commands/commands.cpp)
add_luacs_module(math src/modules/math/math.cpp)

set(MMSOURCE_ROOT "${CMAKE_SOURCE_DIR}/deps/metamod-source" CACHE PATH "Metamod:Source source tree")
set(HL2SDK_CS2_ROOT "${CMAKE_SOURCE_DIR}/deps/hl2sdk-cs2" CACHE PATH "AlliedModders HL2SDK CS2 tree")
set(PROTOBUFS_ROOT "${CMAKE_SOURCE_DIR}/deps/protobufs" CACHE PATH "SteamTracking CS2 protobuf definitions")

if(NOT EXISTS "${MMSOURCE_ROOT}/core/ISmmPlugin.h")
    message(FATAL_ERROR "Metamod source not found at ${MMSOURCE_ROOT}. Run build.bat so dependencies are fetched.")
endif()
if(NOT EXISTS "${HL2SDK_CS2_ROOT}/public/iserver.h")
    message(FATAL_ERROR "HL2SDK CS2 not found at ${HL2SDK_CS2_ROOT}. Run build.bat so dependencies are fetched.")
endif()
if(NOT EXISTS "${PROTOBUFS_ROOT}/csgo/network_connection.proto")
    message(FATAL_ERROR "CS2 protobuf definitions not found at ${PROTOBUFS_ROOT}. Run build.bat so dependencies are fetched.")
endif()

set(LUACS_PROTO_NAMES
    network_connection.proto
    networkbasetypes.proto
    cs_gameevents.proto
    engine_gcmessages.proto
    gcsdk_gcmessages.proto
    cstrike15_gcmessages.proto
    cstrike15_usermessages.proto
    netmessages.proto
    steammessages.proto
    usermessages.proto
    gameevents.proto
    clientmessages.proto
    te.proto)
set(LUACS_PROTO_OUTPUT_DIR "${CMAKE_BINARY_DIR}/protobufcompiler")
file(MAKE_DIRECTORY "${LUACS_PROTO_OUTPUT_DIR}")
set(LUACS_PROTO_INPUTS)
set(LUACS_PROTO_OUTPUTS)
foreach(PROTO_NAME IN LISTS LUACS_PROTO_NAMES)
    list(APPEND LUACS_PROTO_INPUTS "${PROTOBUFS_ROOT}/csgo/${PROTO_NAME}")
    get_filename_component(PROTO_STEM "${PROTO_NAME}" NAME_WE)
    list(APPEND LUACS_PROTO_OUTPUTS
        "${LUACS_PROTO_OUTPUT_DIR}/${PROTO_STEM}.pb.cc"
        "${LUACS_PROTO_OUTPUT_DIR}/${PROTO_STEM}.pb.h")
endforeach()

add_custom_command(
    OUTPUT ${LUACS_PROTO_OUTPUTS}
    COMMAND "${HL2SDK_CS2_ROOT}/devtools/bin/protoc.exe"
        "--proto_path=${HL2SDK_CS2_ROOT}/thirdparty/protobuf-3.21.8/src"
        "--proto_path=${PROTOBUFS_ROOT}/csgo"
        "--cpp_out=${LUACS_PROTO_OUTPUT_DIR}"
        ${LUACS_PROTO_NAMES}
    WORKING_DIRECTORY "${PROTOBUFS_ROOT}/csgo"
    DEPENDS ${LUACS_PROTO_INPUTS}
    COMMENT "Generating CS2 protobuf sources")

add_library(luacs_protobufs STATIC ${LUACS_PROTO_OUTPUTS})
target_include_directories(luacs_protobufs PUBLIC
    "${LUACS_PROTO_OUTPUT_DIR}"
    "${HL2SDK_CS2_ROOT}/thirdparty/protobuf-3.21.8/src")
target_compile_options(luacs_protobufs PRIVATE /bigobj)
target_link_libraries(luacs_protobufs PUBLIC
    "${HL2SDK_CS2_ROOT}/lib/public/win64/2015/libprotobuf.lib")

add_library(luacs_runtime STATIC
    src/runtime/runtime.cpp
    src/runtime/runtime_events.cpp
    src/runtime/runtime_services.cpp
    src/runtime/runtime_lua.cpp
    src/runtime/runtime.h
    src/runtime/runtime_helpers.h)
target_include_directories(luacs_runtime PUBLIC
    include src/common src/runtime thirdparty/lua-5.5.1/src)
target_link_libraries(luacs_runtime PUBLIC lua55 luacs_common)

add_library(luacs2 SHARED
    src/plugin/plugin.cpp
    src/plugin/plugin.h
    "${HL2SDK_CS2_ROOT}/public/tier0/memoverride.cpp"
    "${HL2SDK_CS2_ROOT}/tier1/convar.cpp"
    "${MMSOURCE_ROOT}/core/sourcehook/sourcehook.cpp"
    "${MMSOURCE_ROOT}/core/sourcehook/sourcehook_impl_chookidman.cpp"
    "${MMSOURCE_ROOT}/core/sourcehook/sourcehook_impl_chookmaninfo.cpp"
    "${MMSOURCE_ROOT}/core/sourcehook/sourcehook_impl_cvfnptr.cpp"
    "${MMSOURCE_ROOT}/core/sourcehook/sourcehook_impl_cproto.cpp")

target_include_directories(luacs2 PRIVATE
    include src/common src/runtime src/plugin thirdparty/lua-5.5.1/src
    "${LUACS_PROTO_OUTPUT_DIR}"
    "${MMSOURCE_ROOT}/core"
    "${MMSOURCE_ROOT}/core/sourcehook"
    "${HL2SDK_CS2_ROOT}"
    "${HL2SDK_CS2_ROOT}/public"
    "${HL2SDK_CS2_ROOT}/public/engine"
    "${HL2SDK_CS2_ROOT}/public/entity2"
    "${HL2SDK_CS2_ROOT}/public/game/server"
    "${HL2SDK_CS2_ROOT}/public/mathlib"
    "${HL2SDK_CS2_ROOT}/public/tier0"
    "${HL2SDK_CS2_ROOT}/public/tier1"
    "${HL2SDK_CS2_ROOT}/public/schemasystem"
    "${HL2SDK_CS2_ROOT}/game/shared"
    "${HL2SDK_CS2_ROOT}/game/server"
    "${HL2SDK_CS2_ROOT}/common"
    "${HL2SDK_CS2_ROOT}/thirdparty/protobuf-3.21.8/src")

target_compile_definitions(luacs2 PRIVATE META_IS_SOURCE2 SE_CS2=25)
target_compile_options(luacs2 PRIVATE /permissive-)
target_link_libraries(luacs2 PRIVATE
    luacs_runtime
    luacs_protobufs
    "${HL2SDK_CS2_ROOT}/lib/public/win64/tier0.lib"
    "${HL2SDK_CS2_ROOT}/lib/public/win64/tier1.lib"
    "${HL2SDK_CS2_ROOT}/lib/public/win64/interfaces.lib"
    "${HL2SDK_CS2_ROOT}/lib/public/win64/mathlib.lib"
    ws2_32)
set_target_properties(luacs2 PROPERTIES OUTPUT_NAME "luacs2")
luacs_runtime_output(luacs2 "${LUACS_BIN}")

add_custom_target(luacs_package ALL
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "${LUACS_ROOT}/plugins"
        "${LUACS_ROOT}/config"
        "${LUACS_ROOT}/logs"
        "${LUACS_ROOT}/gamedata/reference"
        "${LUACS_PACKAGE_ROOT}/metamod"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/packaging/addons/metamod/luacs2.vdf"
        "${LUACS_PACKAGE_ROOT}/metamod/luacs2.vdf"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/packaging/LuaCS/scripting/example_welcome.lua"
        "${LUACS_SCRIPTING}/example_welcome.lua"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/packaging/LuaCS/config/README.txt"
        "${LUACS_ROOT}/config/README.txt"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/packaging/LuaCS/plugins/README.txt"
        "${LUACS_ROOT}/plugins/README.txt"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/packaging/LuaCS/logs/README.txt"
        "${LUACS_ROOT}/logs/README.txt"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/gamedata/deprecated_symbols.txt"
        "${LUACS_ROOT}/gamedata/deprecated_symbols.txt"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/gamedata/reference"
        "${LUACS_ROOT}/gamedata/reference"
    DEPENDS luacs2 luacs_compile events timers players commands math lua55 lua55_static)
