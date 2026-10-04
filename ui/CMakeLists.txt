include("${CMAKE_SOURCE_DIR}/engine/cmake/compile_shader.cmake")
set(COMET_UI_SHADER_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/generated/shaders")
compile_shaders(
        TARGET comet_ui_shaders
        COMPILER comet_shader_compiler
        OUTPUT_DIRECTORY "${COMET_UI_SHADER_DIRECTORY}"
        SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/shaders/imgui_hdr.frag"
)

add_library(comet_ui STATIC src/player_input_panel.cpp src/imgui_context.cpp src/input_widgets.cpp)
add_dependencies(comet_ui comet_ui_shaders)
target_include_directories(comet_ui
        PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src"
        PRIVATE "${COMET_UI_SHADER_DIRECTORY}/include")
target_link_libraries(comet_ui PUBLIC engine imgui)
target_compile_options(comet_ui PUBLIC "$<$<CXX_COMPILER_ID:MSVC>:/utf-8>")
