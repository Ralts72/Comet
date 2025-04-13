#pragma once
#include <vulkan/vulkan.hpp>

namespace Comet
{
    class Context;

    class Swapchain
    {
    public:
        Swapchain(Context *context, uint32_t width, uint32_t height);
        ~Swapchain();

        struct SurfaceInfo
        {
            vk::Extent2D imageExtent;
            vk::SurfaceCapabilitiesKHR capabilities;
            uint32_t imageCount;
            vk::SurfaceFormatKHR format;
            vk::PresentModeKHR presentMode;
        };

        struct Image
        {
            vk::Image image;
            vk::ImageView view;
        };

        const SurfaceInfo &getSurfaceInfo() const { return m_surfaceInfo; }

    private:
        void init(uint32_t width, uint32_t height);
        void querySwapchainInfo(uint32_t width, uint32_t height);
        void createImages();

        Context *m_contextHandle;
        vk::SurfaceKHR m_surface;
        SurfaceInfo m_surfaceInfo;

        std::vector<Image> m_images;
        vk::SwapchainKHR m_swapchain;
    };
}