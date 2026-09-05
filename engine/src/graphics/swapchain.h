#pragma once
#include "common/export.h"
#include "graphics/resource/resource_result.h"
#include "vk_common.h"
#include "vk_capability.h"

#include <memory>
#include <vector>

namespace Comet {
    class Context;
    class Device;
    class Image;
    class Semaphore;
    class Fence;
    class Window;

    struct SwapchainCompatibility {
        bool extent_changed = false;
        bool format_changed = false;
        bool image_count_changed = false;
    };

    [[nodiscard]] COMET_API SwapchainCompatibility compare_swapchain_configs(
        const SwapchainConfig& previous, const SwapchainConfig& current);

    class COMET_API Swapchain {
    public:
        class COMET_API Generation {
        public:
            ~Generation();

            Generation(const Generation&) = delete;
            Generation& operator=(const Generation&) = delete;
            Generation(Generation&&) noexcept = delete;
            Generation& operator=(Generation&&) noexcept = delete;

            [[nodiscard]] const vk::SwapchainKHR& get() const { return m_swapchain; }
            [[nodiscard]] const std::vector<std::shared_ptr<Image>>& get_images() const {
                return m_images;
            }
            [[nodiscard]] const SwapchainConfig& get_config() const { return m_config; }
            [[nodiscard]] uint32_t get_current_index() const { return m_current_index; }

        private:
            friend class Swapchain;

            Generation(Device& device, vk::SwapchainKHR swapchain,
                std::vector<std::shared_ptr<Image>> images, SwapchainConfig config);

            Device& m_device;
            vk::SwapchainKHR m_swapchain;
            std::vector<std::shared_ptr<Image>> m_images;
            SwapchainConfig m_config;
            uint32_t m_current_index = static_cast<uint32_t>(-1);
        };

        Swapchain(const Window& window, Context& context, Device& device,
            const SwapchainRequest& request);

        ~Swapchain() = default;

        Swapchain(const Swapchain&) = delete;

        Swapchain& operator=(const Swapchain&) = delete;

        Swapchain(Swapchain&&) noexcept = delete;

        Swapchain& operator=(Swapchain&&) noexcept = delete;

        [[nodiscard]] bool recreate();

        [[nodiscard]] std::pair<uint32_t, vk::Result> acquire_next_image(
            const Semaphore& semaphore);

        [[nodiscard]] uint32_t get_current_index() const;
        [[nodiscard]] const std::vector<std::shared_ptr<Image>>& get_images() const;
        [[nodiscard]] uint32_t get_width() const;
        [[nodiscard]] uint32_t get_height() const;
        [[nodiscard]] const vk::SwapchainKHR& get() const;
        [[nodiscard]] const std::shared_ptr<Generation>& get_active_generation() const;

    private:
        using GenerationResult = GpuResourceResult<std::shared_ptr<Generation>>;

        [[nodiscard]] GenerationResult try_create_generation(
            const SwapchainConfig& config);
        [[nodiscard]] Generation& active_generation();
        [[nodiscard]] const Generation& active_generation() const;

        const Window& m_window;
        Context& m_context;
        Device& m_device;
        std::shared_ptr<Generation> m_active_generation;
        SwapchainRequest m_request;
    };
}
