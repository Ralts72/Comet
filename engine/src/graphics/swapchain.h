#pragma once
#include "vk_common.h"

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
        Swapchain(Context* context, Device* device);

        ~Swapchain();

        bool recreate();

        [[nodiscard]] uint32_t acquire_next_image(const Semaphore& semaphore, const Fence* fence = nullptr);

        void present(uint32_t image_index, std::span<const Semaphore> wait_semaphores) const;

        [[nodiscard]] uint32_t get_current_index() const { return m_current_index; }
        [[nodiscard]] const std::vector<Image>& get_images() const { return m_images; }
        [[nodiscard]] uint32_t get_width() const { return m_surface_info.capabilities.currentExtent.width; }
        [[nodiscard]] uint32_t get_height() const { return m_surface_info.capabilities.currentExtent.height; }

    private:
        void setup_surface_capabilities();

        vk::SwapchainKHR m_swapchain;
        Context* m_context;
        Device* m_device;
        std::vector<Image> m_images;
        SurfaceInfo m_surface_info;
        uint32_t m_current_index = -1;
    };
}
