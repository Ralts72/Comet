cmake_minimum_required(VERSION 3.20)
project(Comet)
set(CMAKE_CXX_STANDARD 17)

set(OUTPATH ${PROJECT_SOURCE_DIR}/build/${CMAKE_BUILD_TYPE})
set(LIBPATH ${PROJECT_SOURCE_DIR}/lib)
set(3RDPATH ${PROJECT_SOURCE_DIR}/engine/3rdParty)
set(SHADER ${PROJECT_SOURCE_DIR}/engine/assets/shaders)

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_definitions(-DDEBUG)
endif()

find_package(Vulkan REQUIRED)
find_program(GLSLC_PROGRAM glslc REQUIRED)
execute_process(COMMAND ${GLSLC_PROGRAM} ${SHADER}/shader.vert -o ${SHADER}/spv/vert.spv)
execute_process(COMMAND ${GLSLC_PROGRAM} ${SHADER}/shader.frag -o ${SHADER}/spv/frag.spv)

include_directories(
    ${3RDPATH}/glfw/include
    ${3RDPATH}/spdlog/include
#     engine/3rdParty/assimp/include
#     engine/3rdParty
    ${Vulkan_INCLUDE_DIRS}
)

add_subdirectory(engine/3rdParty/spdlog)
add_subdirectory(engine/3rdParty/glfw)

# add_subdirectory(engine/3rdParty/assimp)
add_subdirectory(engine/src)
add_subdirectory(app)