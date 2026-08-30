set(TARGET_NAME ${SHADER_COMPILE_TARGET})

set(SHADER_FILES
        "${CMAKE_CURRENT_SOURCE_DIR}/glsl/cube_texture.vert"
        "${CMAKE_CURRENT_SOURCE_DIR}/glsl/cube_texture.frag"
)

source_group("shaders" FILES ${SHADER_FILES})

include("../cmake/compile_shader.cmake")

compile_shaders(
        TARGET ${TARGET_NAME}
        COMPILER "${glslangValidator_executable}"
        OUTPUT_DIRECTORY "${SHADER_GENERATED_DIRECTORY}"
        SOURCES ${SHADER_FILES}
)
