set(TARGET_NAME ${SHADER_COMPILE_TARGET})

# 只编译生产 Shader；其余 GLSL 和头文件保留供学习，不作为隐式构建依赖。
set(SHADER_FILES
        "${CMAKE_CURRENT_SOURCE_DIR}/glsl/unlit_texture_blend.vert"
        "${CMAKE_CURRENT_SOURCE_DIR}/glsl/unlit_texture_blend.frag"
        "${CMAKE_CURRENT_SOURCE_DIR}/glsl/debug_line.vert"
        "${CMAKE_CURRENT_SOURCE_DIR}/glsl/debug_line.frag"
)

source_group("shaders" FILES ${SHADER_FILES})

include("../cmake/compile_shader.cmake")

compile_shaders(
        TARGET ${TARGET_NAME}
        COMPILER "${glslangValidator_executable}"
        OUTPUT_DIRECTORY "${SHADER_GENERATED_DIRECTORY}"
        SOURCES ${SHADER_FILES}
)
