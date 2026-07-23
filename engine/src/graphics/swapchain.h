#pragma once
#include "vk_common.h"
#include "common/config.h"

namespace Comet {
    class Context;
    class Device;
    class Image;
    class Semaphore;
    class Fence;

    struct SurfaceInfo {
        vk::SurfaceCapabilitiesKHR capabilities;
        vk::SurfaceFormatKHR surface_format;
        vk::PresentModeKHR present_mode;
    };

    class Swapchain {
    public:
        Swapchain(Context* context, Device* device, const Config::Vulkan& vulkan_config, const Config::Render& render_config);

        ~Swapchain();

        Swapchain(const Swapchain&) = delete;
        Swapchain& operator=(const Swapchain&) = delete;
        Swapchain(Swapchain&&) noexcept = delete;
        Swapchain& operator=(Swapchain&&) noexcept = delete;

        void recreate();

        [[nodiscard]]std::pair<uint32_t, vk::Result> acquire_next_image(const Semaphore& semaphore);
        [[nodiscard]] uint32_t get_current_index() const { return m_current_index; }
        [[nodiscard]] const std::vector<std::shared_ptr<Image>>& get_images() const { return m_images; }
        [[nodiscard]] uint32_t get_width() const { return m_surface_info.capabilities.currentExtent.width; }
        [[nodiscard]] uint32_t get_height() const { return m_surface_info.capabilities.currentExtent.height; }
        [[nodiscard]] const vk::SwapchainKHR& get() const { return m_swapchain; }

    private:
        void setup_surface_capabilities();

        vk::SwapchainKHR m_swapchain;
        Context* m_context;
        Device* m_device;
        std::vector<std::shared_ptr<Image>> m_images;
        SurfaceInfo m_surface_info;
        uint32_t m_current_index = -1;
        Config::Vulkan m_vulkan_config;
        Config::Render m_render_config;
    };
}
