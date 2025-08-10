cmake_minimum_required(VERSION 3.31)
project(Test)

set(CMAKE_CXX_STANDARD 20)

# 查找Vulkan
find_package(Vulkan REQUIRED)

# 收集所有测试源文件
file(GLOB_RECURSE TEST_SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/*.h"
)

# 创建单元测试可执行文件
add_executable(unit_testing ${TEST_SOURCES})

# 链接库，确保正确的依赖顺序
target_link_libraries(unit_testing PRIVATE
        engine
        gtest_main
        ${Vulkan_LIBRARIES}
)

# 设置包含目录
target_include_directories(unit_testing PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/engine/src
        ${Vulkan_INCLUDE_DIRS}
)

# 设置运行时库路径
if (APPLE)
    set_target_properties(unit_testing PROPERTIES
            INSTALL_RPATH "@executable_path/../engine;@loader_path/../engine;/usr/local/lib"
            BUILD_WITH_INSTALL_RPATH TRUE
    )
elseif (UNIX)
    set_target_properties(unit_testing PROPERTIES
            INSTALL_RPATH "$ORIGIN/../engine:$ORIGIN/../lib:/usr/local/lib"
            BUILD_WITH_INSTALL_RPATH TRUE
    )
endif ()

include(GoogleTest)
gtest_discover_tests(unit_testing)