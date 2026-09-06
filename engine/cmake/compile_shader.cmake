function(compile_shaders)
    # INCLUDE_DIRECTORY 和 DEPENDENCIES 为可选接口，仅传入实际使用的共享 Shader 头文件。
    cmake_parse_arguments(
            SHADER
            ""
            "TARGET;COMPILER;INCLUDE_DIRECTORY;OUTPUT_DIRECTORY;ENTRY_POINT;TARGET_ENVIRONMENT"
            "SOURCES;DEPENDENCIES;DEFINES"
            ${ARGN}
    )

    set(SPV_OUTPUT_DIRECTORY "${SHADER_OUTPUT_DIRECTORY}/spv")
    set(CPP_OUTPUT_DIRECTORY "${SHADER_OUTPUT_DIRECTORY}/include")
    set(SPV_TO_CPP_SCRIPT "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/spv_to_cpp.cmake")
    set(ALL_GENERATED_SPV_FILES)
    set(ALL_GENERATED_CPP_FILES)
    set(SHADER_INCLUDE_ARGUMENT)
    if (SHADER_INCLUDE_DIRECTORY)
        list(APPEND SHADER_INCLUDE_ARGUMENT --include "${SHADER_INCLUDE_DIRECTORY}")
    endif ()
    if (NOT SHADER_ENTRY_POINT)
        set(SHADER_ENTRY_POINT main)
    endif ()
    if (NOT SHADER_TARGET_ENVIRONMENT)
        set(SHADER_TARGET_ENVIRONMENT vulkan1.0)
    endif ()
    foreach (DEFINE IN LISTS SHADER_DEFINES)
        list(APPEND SHADER_INCLUDE_ARGUMENT --define "${DEFINE}")
    endforeach ()

    foreach (SOURCE_FILE IN LISTS SHADER_SOURCES)
        get_filename_component(SHADER_NAME "${SOURCE_FILE}" NAME)
        string(REPLACE "." "_" HEADER_NAME "${SHADER_NAME}")
        string(TOUPPER "${HEADER_NAME}" GLOBAL_SHADER_VAR)
        set(SPV_FILE "${SPV_OUTPUT_DIRECTORY}/${SHADER_NAME}.spv")
        set(CPP_FILE "${CPP_OUTPUT_DIRECTORY}/${HEADER_NAME}.h")
        get_filename_component(STAGE "${SOURCE_FILE}" LAST_EXT)
        string(SUBSTRING "${STAGE}" 1 -1 STAGE)

        add_custom_command(
                OUTPUT "${SPV_FILE}"
                COMMAND "${CMAKE_COMMAND}" -E make_directory "${SPV_OUTPUT_DIRECTORY}"
                COMMAND "$<TARGET_FILE:${SHADER_COMPILER}>"
                        ${SHADER_INCLUDE_ARGUMENT}
                        --stage "${STAGE}" --entry "${SHADER_ENTRY_POINT}"
                        --target "${SHADER_TARGET_ENVIRONMENT}"
                        --output "${SPV_FILE}" --depfile "${SPV_FILE}.d"
                        --source "${SOURCE_FILE}"
                DEPENDS ${SHADER_COMPILER} "${SOURCE_FILE}" ${SHADER_DEPENDENCIES}
                DEPFILE "${SPV_FILE}.d"
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
