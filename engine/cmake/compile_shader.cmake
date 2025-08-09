function(compile_shader SHADERS TARGET_NAME SHADER_INCLUDE_FOLDER GLSLANG_BIN)
    set(working_dir "${CMAKE_CURRENT_SOURCE_DIR}")
    set(ALL_GENERATED_SPV_FILES "")
    set(ALL_GENERATED_CPP_FILES "")
    if (UNIX)
        execute_process(COMMAND chmod a+x ${GLSLANG_BIN})
    endif ()

    foreach (SHADER ${SHADERS})
        get_filename_component(SHADER_NAME ${SHADER} NAME)
        string(REPLACE "." "_" HEADER_NAME ${SHADER_NAME})
        string(TOUPPER ${HEADER_NAME} GLOBAL_SHADER_VAR)
        set(SPV_FILE "${CMAKE_CURRENT_SOURCE_DIR}/spv/${SHADER_NAME}.spv")
        set(CPP_FILE "${CMAKE_CURRENT_SOURCE_DIR}/include/${HEADER_NAME}.h")
        add_custom_command(
                OUTPUT ${SPV_FILE}
                COMMAND ${GLSLANG_BIN} -I${SHADER_INCLUDE_FOLDER} -V100 -o ${SPV_FILE} ${SHADER}
                DEPENDS ${SHADER}
                WORKING_DIRECTORY "${working_dir}")

        list(APPEND ALL_GENERATED_SPV_FILES ${SPV_FILE})

        add_custom_command(
                OUTPUT ${CPP_FILE}
                COMMAND ${CMAKE_COMMAND}
                -DSPV_FILE="${SPV_FILE}"
                -DCPP_FILE="${CPP_FILE}"
                -DGLOBAL_VAR="${GLOBAL_SHADER_VAR}"
                -DEMBED_RESOURCE_ENTRY=1
                -P "${CMAKE_SOURCE_DIR}/engine/cmake/spv_to_cpp.cmake"
                DEPENDS ${SPV_FILE}
                WORKING_DIRECTORY "${working_dir}")

        list(APPEND ALL_GENERATED_CPP_FILES ${CPP_FILE})

    endforeach ()

    add_custom_target(${TARGET_NAME}
            DEPENDS ${ALL_GENERATED_SPV_FILES} ${ALL_GENERATED_CPP_FILES} SOURCES ${SHADERS})

endfunction()