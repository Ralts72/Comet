include("${CMAKE_SOURCE_DIR}/engine/cmake/compile_shader.cmake")
set(COMET_UI_SHADER_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/src/ui/generated/shaders")
compile_shaders(
        TARGET comet_game_ui_shaders
        COMPILER comet_shader_compiler
        OUTPUT_DIRECTORY "${COMET_UI_SHADER_DIRECTORY}"
        SOURCES
        "${CMAKE_SOURCE_DIR}/engine/shaders/ui/rml_ui.vert"
        "${CMAKE_SOURCE_DIR}/engine/shaders/ui/rml_ui.frag")

set(COMET_UI_FONT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/src/ui/fonts")
add_custom_target(comet_game_ui_fonts
        COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different
                "${CMAKE_SOURCE_DIR}/engine/resources/fonts" "${COMET_UI_FONT_DIRECTORY}"
        VERBATIM)
comet_add_module(game_ui)
add_dependencies(comet_game_ui comet_game_ui_shaders comet_game_ui_fonts)
target_include_directories(comet_game_ui PRIVATE "${COMET_UI_SHADER_DIRECTORY}/include")
target_include_directories(comet_game_ui SYSTEM PRIVATE "${CMAKE_SOURCE_DIR}/3rdparty/stb_image")
target_link_libraries(comet_game_ui PRIVATE comet_render comet_platform RmlUi::Core comet_lua)
target_compile_definitions(comet_game_ui PRIVATE
        COMET_UI_FONT_DIRECTORY="${COMET_UI_FONT_DIRECTORY}")
target_compile_options(comet_game_ui PRIVATE "$<$<CXX_COMPILER_ID:MSVC>:/utf-8>")
