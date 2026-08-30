function(compile_shaders)
    cmake_parse_arguments(
            SHADER
            ""
            "TARGET;COMPILER;INCLUDE_DIRECTORY;OUTPUT_DIRECTORY"
            "SOURCES;DEPENDENCIES"
            ${ARGN}
    )

    set(SPV_OUTPUT_DIRECTORY "${SHADER_OUTPUT_DIRECTORY}/spv")
    set(CPP_OUTPUT_DIRECTORY "${SHADER_OUTPUT_DIRECTORY}/include")
    set(SPV_TO_CPP_SCRIPT "${CMAKE_SOURCE_DIR}/engine/cmake/spv_to_cpp.cmake")
    set(ALL_GENERATED_SPV_FILES)
    set(ALL_GENERATED_CPP_FILES)
    set(SHADER_INCLUDE_ARGUMENT)
    if (SHADER_INCLUDE_DIRECTORY)
        list(APPEND SHADER_INCLUDE_ARGUMENT
                "-I${SHADER_INCLUDE_DIRECTORY}")
    endif ()

    foreach (SOURCE_FILE IN LISTS SHADER_SOURCES)
        get_filename_component(SHADER_NAME "${SOURCE_FILE}" NAME)
        string(REPLACE "." "_" HEADER_NAME "${SHADER_NAME}")
        string(TOUPPER "${HEADER_NAME}" GLOBAL_SHADER_VAR)
        set(SPV_FILE "${SPV_OUTPUT_DIRECTORY}/${SHADER_NAME}.spv")
        set(CPP_FILE "${CPP_OUTPUT_DIRECTORY}/${HEADER_NAME}.h")

        add_custom_command(
                OUTPUT "${SPV_FILE}"
                COMMAND "${CMAKE_COMMAND}" -E make_directory "${SPV_OUTPUT_DIRECTORY}"
                COMMAND "${SHADER_COMPILER}"
                        ${SHADER_INCLUDE_ARGUMENT}
                        -V100
                        -o "${SPV_FILE}"
                        "${SOURCE_FILE}"
                DEPENDS "${SOURCE_FILE}" ${SHADER_DEPENDENCIES}
                VERBATIM
        )

        list(APPEND ALL_GENERATED_SPV_FILES "${SPV_FILE}")

        add_custom_command(
                OUTPUT "${CPP_FILE}"
                COMMAND "${CMAKE_COMMAND}" -E make_directory "${CPP_OUTPUT_DIRECTORY}"
                COMMAND "${CMAKE_COMMAND}"
                        "-DSPV_FILE=${SPV_FILE}"
                        "-DCPP_FILE=${CPP_FILE}"
                        "-DGLOBAL_VAR=${GLOBAL_SHADER_VAR}"
                        -DEMBED_RESOURCE_ENTRY=1
                        -P "${SPV_TO_CPP_SCRIPT}"
                DEPENDS "${SPV_FILE}" "${SPV_TO_CPP_SCRIPT}"
                VERBATIM
        )

        list(APPEND ALL_GENERATED_CPP_FILES "${CPP_FILE}")
    endforeach ()

    add_custom_target(${SHADER_TARGET}
            DEPENDS ${ALL_GENERATED_SPV_FILES} ${ALL_GENERATED_CPP_FILES}
            SOURCES ${SHADER_SOURCES} ${SHADER_DEPENDENCIES}
    )
endfunction()
