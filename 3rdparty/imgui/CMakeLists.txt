cmake_minimum_required(VERSION 3.31)
project(imgui)

find_package(Vulkan REQUIRED)

add_library(imgui STATIC
    imgui.cpp
    imgui_draw.cpp
    imgui_tables.cpp
    imgui_widgets.cpp
    backends/imgui_impl_glfw.cpp
    backends/imgui_impl_vulkan.cpp
)

target_include_directories(imgui PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/backends
    ${Vulkan_INCLUDE_DIRS}
)
target_link_libraries(imgui PUBLIC glfw  ${Vulkan_LIBRARIES})
target_compile_features(imgui PUBLIC cxx_std_20)
