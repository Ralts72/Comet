macro(compile_shader shader_name output_name)
    if (GLSLC_PROGRAM)
        message(STATUS "compiling shader ${CMAKE_CURRENT_SOURCE_DIR}/${shader_name} to ${output_name}")
        execute_process(
                COMMAND ${GLSLC_PROGRAM} ${CMAKE_CURRENT_SOURCE_DIR}/${shader_name} -o ${CMAKE_CURRENT_SOURCE_DIR}/${output_name}
                WORKING_DIRECTORY ..)
    else ()
        message(STATUS "compiling shader ${CMAKE_CURRENT_SOURCE_DIR}: ${shader_name} --> ${output_name}")
    endif ()
endmacro()