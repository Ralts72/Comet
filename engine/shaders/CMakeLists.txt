set(TARGET_NAME ${SHADER_COMPILE_TARGET})

set(SHADER_PROGRAMS
        material/unlit_color
        material/pbr
        debug/line
        particles/billboard
        post/display
        post/bloom
        environment/skybox
        depth/depth
        depth/motion
)
set(SHADER_FILES)
foreach(PROGRAM IN LISTS SHADER_PROGRAMS)
    list(APPEND SHADER_FILES
            "${CMAKE_CURRENT_SOURCE_DIR}/${PROGRAM}.vert"
            "${CMAKE_CURRENT_SOURCE_DIR}/${PROGRAM}.frag")
endforeach()
list(APPEND SHADER_FILES
        "${CMAKE_CURRENT_SOURCE_DIR}/animation/skinning.comp"
        "${CMAKE_CURRENT_SOURCE_DIR}/path_trace.comp"
        "${CMAKE_CURRENT_SOURCE_DIR}/depth/depth_mask.frag"
        "${CMAKE_CURRENT_SOURCE_DIR}/depth/motion_mask.frag"
        "${CMAKE_CURRENT_SOURCE_DIR}/post/temporal.frag"
        "${CMAKE_CURRENT_SOURCE_DIR}/post/decal.frag"
        "${CMAKE_CURRENT_SOURCE_DIR}/post/deferred.frag"
        "${CMAKE_CURRENT_SOURCE_DIR}/post/screen_space_reflection.frag"
        "${CMAKE_CURRENT_SOURCE_DIR}/material/pbr_gbuffer.frag"
        "${CMAKE_CURRENT_SOURCE_DIR}/material/unlit_gbuffer.frag"
        "${CMAKE_CURRENT_SOURCE_DIR}/post/volumetric_fog.comp"
        "${CMAKE_CURRENT_SOURCE_DIR}/post/fog_background.frag"
        "${CMAKE_CURRENT_SOURCE_DIR}/material/pbr_instanced.vert"
        "${CMAKE_CURRENT_SOURCE_DIR}/material/unlit_color_instanced.vert")

source_group("shaders" FILES ${SHADER_FILES})

include("../cmake/compile_shader.cmake")

compile_shaders(
        TARGET ${TARGET_NAME}
        COMPILER comet_shader_compiler
        OUTPUT_DIRECTORY "${SHADER_GENERATED_DIRECTORY}"
        SOURCES ${SHADER_FILES}
)
