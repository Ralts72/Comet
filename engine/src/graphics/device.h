#pragma once
#include <vulkan/vulkan.hpp>

namespace Comet {
    class Context;
    class Queue;

    struct VkSettings {
        vk::Format surface_format = vk::Format::eB8G8R8A8Unorm;
        vk::Format depth_format = vk::Format::eD32Sfloat;
        vk::PresentModeKHR present_mode = vk::PresentModeKHR::eImmediate;
        uint32_t swapchain_image_count = 3;
    };

    class Device {
    public:
        Device(Context* context, uint32_t graphics_queue_count, uint32_t present_queue_count, const VkSettings& settings = {});

        ~Device();

        [[nodiscard]] vk::Device get_device() const { return m_device; }
        [[nodiscard]] const VkSettings& get_settings() const { return m_settings; }

        [[nodiscard]] std::shared_ptr<Queue> get_graphics_queue(const uint32_t index = 0) const {
            return m_graphics_queues.at(index);
        }

        [[nodiscard]] std::shared_ptr<Queue> get_present_queue(const uint32_t index = 0) const {
            return m_present_queues.at(index);
        }

    private:
        vk::Device m_device;
        Context* m_context;
        VkSettings m_settings;

        std::vector<std::shared_ptr<Queue>> m_graphics_queues;
        std::vector<std::shared_ptr<Queue>> m_present_queues;
    };
}
