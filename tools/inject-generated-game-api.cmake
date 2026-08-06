function(luacs_inject_generated_sources)
    get_target_property(luacs_sources luacs2 SOURCES)
    if(NOT luacs_sources)
        message(FATAL_ERROR
            "LuaCS target sources were unavailable during generated-source injection.")
    endif()

    set(found_original_game_api FALSE)
    set(found_original_plugin FALSE)
    foreach(source IN LISTS luacs_sources)
        if(source MATCHES "(^|[/\\\\])game_api\\.cpp$")
            set_source_files_properties(
                "${source}"
                TARGET_DIRECTORY luacs2
                PROPERTIES HEADER_FILE_ONLY TRUE)
            set(found_original_game_api TRUE)
        elseif(source MATCHES "(^|[/\\\\])plugin\\.cpp$")
            set_source_files_properties(
                "${source}"
                TARGET_DIRECTORY luacs2
                PROPERTIES HEADER_FILE_ONLY TRUE)
            set(found_original_plugin TRUE)
        endif()
    endforeach()

    if(NOT found_original_game_api)
        message(FATAL_ERROR
            "The original game_api.cpp source was not found in the LuaCS target.")
    endif()
    if(NOT found_original_plugin)
        message(FATAL_ERROR
            "The original plugin.cpp source was not found in the LuaCS target.")
    endif()

    set(generated_game_api
        "${CMAKE_BINARY_DIR}/generated/plugin/game_api.cpp")
    set(generated_plugin
        "${CMAKE_BINARY_DIR}/generated/plugin/plugin.cpp")
    set(server_module_source
        "${CMAKE_SOURCE_DIR}/src/plugin/server_module.cpp")

    foreach(required_source IN ITEMS
            "${generated_game_api}"
            "${generated_plugin}"
            "${server_module_source}")
        if(NOT EXISTS "${required_source}")
            message(FATAL_ERROR
                "Required LuaCS generated/module source is missing: ${required_source}")
        endif()
    endforeach()

    target_sources(luacs2 PRIVATE
        "${generated_game_api}"
        "${generated_plugin}"
        "${server_module_source}")
    message(STATUS
        "Using generated disk-backed scanner and live game server module binding")
endfunction()

cmake_language(DEFER CALL luacs_inject_generated_sources)
