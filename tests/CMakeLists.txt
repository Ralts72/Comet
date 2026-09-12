file(GLOB_RECURSE TEST_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
)
add_executable(unit_testing ${TEST_SOURCES})
include("${CMAKE_SOURCE_DIR}/engine/cmake/compile_shader.cmake")
compile_shaders(
        TARGET test_shader_compile
        COMPILER comet_shader_compiler
        OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/generated/shaders"
        SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/graphics/shaders/interface_array.vert"
        "${CMAKE_CURRENT_SOURCE_DIR}/graphics/shaders/material_integer.frag"
        "${CMAKE_CURRENT_SOURCE_DIR}/graphics/shaders/runtime_array.frag"
        "${CMAKE_CURRENT_SOURCE_DIR}/graphics/shaders/pipeline_triangle.vert"
        "${CMAKE_CURRENT_SOURCE_DIR}/graphics/shaders/pipeline_color.frag"
)
add_dependencies(unit_testing shader_compile test_shader_compile)
target_include_directories(unit_testing PRIVATE
        "${CMAKE_BINARY_DIR}/engine/generated/shaders/include"
        "${CMAKE_CURRENT_BINARY_DIR}/generated/shaders/include")

target_link_libraries(unit_testing PRIVATE editor_core comet_shader_tools gtest glfw)
target_compile_definitions(unit_testing PRIVATE GLFW_INCLUDE_NONE)
comet_copy_runtime_dependencies(unit_testing)

if (COMET_BUILD_EDITOR)
    target_link_libraries(unit_testing PRIVATE editor_ui)
    target_compile_definitions(unit_testing PRIVATE COMET_TEST_EDITOR_UI)
endif ()

target_include_directories(unit_testing PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

add_test(NAME unit_testing COMMAND unit_testing)
add_test(NAME shader_build_contract COMMAND "${CMAKE_COMMAND}"
        "-DCOMPILE_TOOL=$<TARGET_FILE:comet_shader_compiler>"
        "-DCOMET_ROOT=${CMAKE_SOURCE_DIR}"
        "-DTEST_ROOT=${CMAKE_CURRENT_BINARY_DIR}/shader-build-contract"
        "-DGENERATOR=${CMAKE_GENERATOR}"
        "-DBUILD_CONFIG=$<CONFIG>"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/shader/test_build.cmake")
