add_library(comet_demo STATIC scripts.cpp)
target_link_libraries(comet_demo PUBLIC engine)
target_include_directories(comet_demo PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")
