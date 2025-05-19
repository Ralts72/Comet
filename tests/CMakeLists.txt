cmake_minimum_required(VERSION 3.31)
project(Test)

set(CMAKE_CXX_STANDARD 17)

add_executable(test_runner test.cpp)
target_link_libraries(test_runner PRIVATE gtest_main)

include(GoogleTest)
gtest_discover_tests(test_runner)
target_link_libraries(test_runner PRIVATE engine)