option(COMET_USE_COMPILER_CACHE "Use ccache when available and no compiler launcher is configured" ON)

# Launcher variables remain normal variables: an automatic choice must not become
# an explicit user setting in CMakeCache.txt on the next configure.
set(comet_ccache_languages)
foreach(language C CXX)
    if(NOT DEFINED CMAKE_${language}_COMPILER_LAUNCHER AND
        (CMAKE_${language}_COMPILER_ID STREQUAL "GNU" OR
            (CMAKE_${language}_COMPILER_ID MATCHES "^(Clang|AppleClang)$" AND
                CMAKE_${language}_COMPILER_FRONTEND_VARIANT STREQUAL "GNU")))
        list(APPEND comet_ccache_languages "${language}")
    endif()
endforeach()
if(COMET_USE_COMPILER_CACHE AND CMAKE_GENERATOR MATCHES "Makefiles|Ninja" AND
    comet_ccache_languages)
    find_program(COMET_CCACHE_EXECUTABLE NAMES ccache)
    if(COMET_CCACHE_EXECUTABLE)
        execute_process(COMMAND "${COMET_CCACHE_EXECUTABLE}" --get-config sloppiness
            RESULT_VARIABLE comet_ccache_config_result
            OUTPUT_VARIABLE comet_ccache_sloppiness
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET)
        if(comet_ccache_config_result STREQUAL "0")
            string(REPLACE "," ";" comet_ccache_sloppiness "${comet_ccache_sloppiness}")
            list(APPEND comet_ccache_sloppiness pch_defines time_macros)
            list(REMOVE_DUPLICATES comet_ccache_sloppiness)
            list(JOIN comet_ccache_sloppiness "," comet_ccache_sloppiness)
            set(comet_ccache_launcher "${CMAKE_COMMAND}" -E env
                "CCACHE_SLOPPINESS=${comet_ccache_sloppiness}" "${COMET_CCACHE_EXECUTABLE}")
            # A deliberately empty launcher also disables automatic selection.
            foreach(language IN LISTS comet_ccache_languages)
                set(CMAKE_${language}_COMPILER_LAUNCHER "${comet_ccache_launcher}")
                set(COMET_AUTOMATIC_${language}_CCACHE ON)
            endforeach()
            if(COMET_AUTOMATIC_CXX_CCACHE)
                # CMake uses -include-pch. Clang must omit PCH timestamps so that
                # equivalent regenerated headers can reuse cached objects.
                add_compile_options(
                    "$<$<COMPILE_LANG_AND_ID:CXX,Clang,AppleClang>:SHELL:-Xclang -fno-pch-timestamp>")
            endif()
            if(COMET_AUTOMATIC_C_CCACHE OR COMET_AUTOMATIC_CXX_CCACHE)
                message(STATUS "Comet compiler cache: ${COMET_CCACHE_EXECUTABLE} (PCH compatible)")
                add_custom_target(comet_cache_stats
                    COMMAND "${COMET_CCACHE_EXECUTABLE}" --show-stats
                    COMMENT "Compiler cache statistics")
            endif()
        else()
            message(STATUS "Comet compiler cache: automatic ccache configuration unavailable")
        endif()
    else()
        message(STATUS "Comet compiler cache: ccache not found")
    endif()
endif()
