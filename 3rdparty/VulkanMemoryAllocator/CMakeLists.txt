cmake_minimum_required(VERSION 3.31)
project(VulkanMemoryAllocator)

add_library(VulkanMemoryAllocator STATIC vk_mem_alloc.cpp)
add_library(GPUOpen::VulkanMemoryAllocator ALIAS VulkanMemoryAllocator)

target_include_directories(VulkanMemoryAllocator
        PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(VulkanMemoryAllocator PUBLIC Vulkan::Vulkan)
