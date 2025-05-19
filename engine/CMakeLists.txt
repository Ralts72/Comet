cmake_minimum_required(VERSION 3.31)
project(Comet)

set(CMAKE_CXX_STANDARD 17)

file(GLOB SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")

add_library(engine ${SOURCES})
target_link_libraries(engine PUBLIC SDL3::SDL3 spdlog::spdlog volk::volk glm)
target_compile_definitions(engine PUBLIC SDL_MAIN_USE_CALLBACKS)
target_include_directories(engine
        PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_precompile_headers(engine PUBLIC "src/pch.h")
