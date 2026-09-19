set(TARGET_NAME ${SHADER_COMPILE_TARGET})

set(SHADER_PROGRAMS
        material/unlit_texture_blend
        material/unlit_color
        material/lambert
        debug/line
        post/display
        shadow/directional
)
set(SHADER_FILES)
foreach(PROGRAM IN LISTS SHADER_PROGRAMS)
    list(APPEND SHADER_FILES
            "${CMAKE_CURRENT_SOURCE_DIR}/${PROGRAM}.vert"
            "${CMAKE_CURRENT_SOURCE_DIR}/${PROGRAM}.frag")
endforeach()

source_group("shaders" FILES ${SHADER_FILES})

include("../cmake/compile_shader.cmake")

compile_shaders(
        TARGET ${TARGET_NAME}
        COMPILER comet_shader_compiler
        OUTPUT_DIRECTORY "${SHADER_GENERATED_DIRECTORY}"
        SOURCES ${SHADER_FILES}
)
