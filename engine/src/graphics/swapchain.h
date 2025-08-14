#pragma once
#include "vk_common.h"

namespace Comet {
    class Context;
    class Device;

    struct SurfaceInfo {
        vk::SurfaceCapabilitiesKHR capabilities;
        vk::SurfaceFormatKHR surface_format;
        vk::PresentModeKHR present_mode;
    };

    class Swapchain {
    public:
        Swapchain(Context* context, Device* device);

        ~Swapchain();

        bool recreate();
        [[nodiscard]] const std::vector<vk::Image>& get_images() const { return m_images; }
        [[nodiscard]] uint32_t get_width() const { return m_surface_info.capabilities.currentExtent.width; }
        [[nodiscard]] uint32_t get_height() const { return m_surface_info.capabilities.currentExtent.height; }

    private:
        void setup_surface_capabilities();

        vk::SwapchainKHR m_swapchain;
        Context* m_context;
        Device* m_device;
        std::vector<vk::Image> m_images;
        SurfaceInfo m_surface_info;
    };
}
