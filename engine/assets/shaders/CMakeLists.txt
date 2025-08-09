set(TARGET_NAME ${SHADER_COMPILE_TARGET})

file(GLOB_RECURSE SHADER_FILES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/glsl/*.vert"
        "${CMAKE_CURRENT_SOURCE_DIR}/glsl/*.frag"
        "${CMAKE_CURRENT_SOURCE_DIR}/glsl/*.comp"
        "${CMAKE_CURRENT_SOURCE_DIR}/glsl/*.geom")

SOURCE_GROUP("shaders" FILES ${SHADER_FILES})

set(SHADER_INCLUDE_FOLDER ${CMAKE_CURRENT_SOURCE_DIR}/include)

if (NOT EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/spv)
    file(MAKE_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/spv)
endif()

include("../../cmake/compile_shader.cmake")

compile_shader(
        "${SHADER_FILES}"
        "${TARGET_NAME}"
        "${SHADER_INCLUDE_FOLDER}"
        "${glslangValidator_executable}")