#include "window_surface.h"
#include "core/window.h"

#include <GLFW/glfw3.h>

namespace Comet::Detail {
    Result<std::span<const char* const>, GraphicsError> window_instance_extensions() {
        using Extensions = Result<std::span<const char* const>, GraphicsError>;
        unsigned int count = 0;
        const auto* extensions = glfwGetRequiredInstanceExtensions(&count);
        if(!extensions || count == 0)
            return Extensions::failure(
                {"GLFW did not provide the required Vulkan instance extensions"});
        return Extensions::success({extensions, count});
    }

    Result<vk::UniqueSurfaceKHR, GraphicsError> create_window_surface(
        vk::Instance instance, const Window& window) {
        using Creation = Result<vk::UniqueSurfaceKHR, GraphicsError>;
        if(!window.get())
            return Creation::failure({"GLFW window not created"});
        VkSurfaceKHR surface = VK_NULL_HANDLE;
        const auto result = static_cast<vk::Result>(
            glfwCreateWindowSurface(instance, window.get(), nullptr, &surface));
        if(result != vk::Result::eSuccess)
            return Creation::failure({"Cannot create window surface", result});
        return Creation::success(vk::UniqueSurfaceKHR(vk::SurfaceKHR(surface), {instance}));
    }
}
