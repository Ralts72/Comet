#pragma once
#include "vk_common.h"
#include "vk_capability.h"

#include <vector>

namespace Comet {
    class Context;
    class Device;
    class Image;
    class Semaphore;
    class Fence;
    class Window;

    class Swapchain {
    public:
        Swapchain(const Window& window, Context& context, Device& device,
            const SwapchainRequest& request);

        ~Swapchain();

        Swapchain(const Swapchain&) = delete;

        Swapchain& operator=(const Swapchain&) = delete;

        Swapchain(Swapchain&&) noexcept = delete;

        Swapchain& operator=(Swapchain&&) noexcept = delete;

        [[nodiscard]] bool recreate();

        [[nodiscard]] std::pair<uint32_t, vk::Result> acquire_next_image(
            const Semaphore& semaphore);

        [[nodiscard]] uint32_t get_current_index() const { return m_current_index; }
        [[nodiscard]] const std::vector<std::shared_ptr<Image>>& get_images() const {
            return m_images;
        }
        [[nodiscard]] uint32_t get_width() const { return m_config.extent.width; }
        [[nodiscard]] uint32_t get_height() const { return m_config.extent.height; }
        [[nodiscard]] const vk::SwapchainKHR& get() const { return m_swapchain; }

    private:
        vk::SwapchainKHR m_swapchain;
        const Window& m_window;
        Context& m_context;
        Device& m_device;
        std::vector<std::shared_ptr<Image>> m_images;
        SwapchainConfig m_config;
        uint32_t m_current_index = -1;
        SwapchainRequest m_request;
    };
}
