function(luacs_inject_generated_game_api)
    get_target_property(luacs_sources luacs2 SOURCES)
    if(NOT luacs_sources)
        message(FATAL_ERROR "LuaCS target sources were unavailable during game API injection.")
    endif()

    set(found_original FALSE)
    foreach(source IN LISTS luacs_sources)
        if(source MATCHES "(^|[/\\\\])game_api\\.cpp$")
            set_source_files_properties(
                "${source}"
                TARGET_DIRECTORY luacs2
                PROPERTIES HEADER_FILE_ONLY TRUE)
            set(found_original TRUE)
        endif()
    endforeach()

    if(NOT found_original)
        message(FATAL_ERROR "The original game_api.cpp source was not found in the LuaCS target.")
    endif()

    set(generated_source
        "${CMAKE_BINARY_DIR}/generated/plugin/game_api.cpp")
    if(NOT EXISTS "${generated_source}")
        message(FATAL_ERROR
            "Generated disk-backed game API source is missing: ${generated_source}")
    endif()

    target_sources(luacs2 PRIVATE "${generated_source}")
    message(STATUS "Using generated disk-backed CS2 signature scanner")
endfunction()

cmake_language(DEFER CALL luacs_inject_generated_game_api)
