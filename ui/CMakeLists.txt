add_library(comet_ui STATIC src/player_input_panel.cpp)
target_include_directories(comet_ui PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(comet_ui PUBLIC engine imgui)
target_compile_options(comet_ui PUBLIC "$<$<CXX_COMPILER_ID:MSVC>:/utf-8>")
