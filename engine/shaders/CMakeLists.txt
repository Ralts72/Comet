set(TARGET_NAME ${SHADER_COMPILE_TARGET})

# 只编译生产 Shader；其余 GLSL 和头文件保留供学习，不作为隐式构建依赖。
set(SHADER_FILES
        "${CMAKE_CURRENT_SOURCE_DIR}/glsl/material_mesh.vert"
        "${CMAKE_CURRENT_SOURCE_DIR}/glsl/material_textured.frag"
        "${CMAKE_CURRENT_SOURCE_DIR}/glsl/material_solid.frag"
        "${CMAKE_CURRENT_SOURCE_DIR}/glsl/material_lit.vert"
        "${CMAKE_CURRENT_SOURCE_DIR}/glsl/material_lit.frag"
        "${CMAKE_CURRENT_SOURCE_DIR}/glsl/material_pbr.frag"
        "${CMAKE_CURRENT_SOURCE_DIR}/glsl/shadow_depth.vert"
        "${CMAKE_CURRENT_SOURCE_DIR}/glsl/shadow_depth.frag"
        "${CMAKE_CURRENT_SOURCE_DIR}/glsl/debug_line.vert"
        "${CMAKE_CURRENT_SOURCE_DIR}/glsl/debug_line.frag"
        "${CMAKE_CURRENT_SOURCE_DIR}/glsl/fullscreen.vert"
        "${CMAKE_CURRENT_SOURCE_DIR}/glsl/tone_map.frag"
)

source_group("shaders" FILES ${SHADER_FILES})

include("../cmake/compile_shader.cmake")

compile_shaders(
        TARGET ${TARGET_NAME}
        COMPILER comet_shader_compiler
        OUTPUT_DIRECTORY "${SHADER_GENERATED_DIRECTORY}"
        SOURCES ${SHADER_FILES}
)
