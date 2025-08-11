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

    private:
        void setup_surface_capabilities();

        vk::SwapchainKHR m_swapchain;
        Context* m_context;
        Device* m_device;
        std::vector<vk::Image> m_images;
        SurfaceInfo m_surface_info;
    };
}
