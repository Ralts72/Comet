#pragma once
#include "vk_common.h"
#include "vk_capability.h"
#include "common/export.h"
#include "graphics/resource/resource_result.h"

#include <memory>
#include <vector>

namespace Comet {
    class Context;
    class Device;
    class Image;
    class Semaphore;
    class Fence;
    class Window;

    class COMET_API SwapchainGeneration {
    public:
        ~SwapchainGeneration();

        SwapchainGeneration(const SwapchainGeneration&) = delete;
        SwapchainGeneration& operator=(const SwapchainGeneration&) = delete;
        SwapchainGeneration(SwapchainGeneration&&) noexcept = delete;
        SwapchainGeneration& operator=(SwapchainGeneration&&) noexcept = delete;

        [[nodiscard]] const vk::SwapchainKHR& get() const {
            return m_swapchain;
        }
        [[nodiscard]] const std::vector<std::shared_ptr<Image>>& get_images() const {
            return m_images;
        }
        [[nodiscard]] const SwapchainConfig& get_config() const {
            return m_config;
        }
        [[nodiscard]] uint32_t get_current_index() const {
            return m_current_index;
        }

    private:
        friend class Swapchain;

        SwapchainGeneration(
            Device& device,
            vk::SwapchainKHR swapchain,
            std::vector<std::shared_ptr<Image>> images,
            SwapchainConfig config);

        Device& m_device;
        vk::SwapchainKHR m_swapchain;
        std::vector<std::shared_ptr<Image>> m_images;
        SwapchainConfig m_config;
        uint32_t m_current_index = static_cast<uint32_t>(-1);
    };

    class COMET_API Swapchain {
    public:
        Swapchain(const Window& window, Context& context, Device& device,
                  const SwapchainRequest& request);

        ~Swapchain();

        Swapchain(const Swapchain&) = delete;

        Swapchain& operator=(const Swapchain&) = delete;

        Swapchain(Swapchain&&) noexcept = delete;

        Swapchain& operator=(Swapchain&&) noexcept = delete;

        [[nodiscard]] bool recreate();

        [[nodiscard]] std::pair<uint32_t, vk::Result> acquire_next_image(const Semaphore& semaphore);

        [[nodiscard]] uint32_t get_current_index() const;
        [[nodiscard]] const std::vector<std::shared_ptr<Image>>& get_images() const;
        [[nodiscard]] uint32_t get_width() const;
        [[nodiscard]] uint32_t get_height() const;
        [[nodiscard]] const vk::SwapchainKHR& get() const;
        [[nodiscard]] const std::shared_ptr<SwapchainGeneration>&
        get_active_generation() const { return m_active_generation; }

    private:
        [[nodiscard]] GpuResourceResult<std::shared_ptr<SwapchainGeneration>>
        try_create_generation(const SwapchainConfig& config);
        [[nodiscard]] SwapchainGeneration& active_generation();
        [[nodiscard]] const SwapchainGeneration& active_generation() const;

        const Window& m_window;
        Context& m_context;
        Device& m_device;
        std::shared_ptr<SwapchainGeneration> m_active_generation;
        SwapchainRequest m_request;
    };
}
