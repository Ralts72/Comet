#pragma once
#include "vk_common.h"

namespace Comet {
    class Context;
    class Queue;
    class Fence;

    struct VkSettings {
        vk::Format surface_format = vk::Format::eB8G8R8A8Unorm;
        vk::ColorSpaceKHR color_space = vk::ColorSpaceKHR::eSrgbNonlinear;
        vk::Format depth_format = vk::Format::eD32Sfloat;
        vk::PresentModeKHR present_mode = vk::PresentModeKHR::eImmediate;
        uint32_t swapchain_image_count = 3;
    };

    class Device {
    public:
        Device(Context* context, uint32_t graphics_queue_count, uint32_t present_queue_count, const VkSettings& settings = {});

        ~Device();

        void wait_for_fences(std::span<const Fence> fences, bool wait_all = true,
                             uint64_t timeout = std::numeric_limits<uint64_t>::max()) const;

        void reset_fences(std::span<const Fence> fences) const;

        void wait_idle();

        [[nodiscard]] vk::Device get_device() const { return m_device; }
        [[nodiscard]] const VkSettings& get_settings() const { return m_settings; }

        [[nodiscard]] std::shared_ptr<Queue> get_graphics_queue(const uint32_t index = 0) const {
            return m_graphics_queues.at(index);
        }

        [[nodiscard]] std::shared_ptr<Queue> get_present_queue(const uint32_t index = 0) const {
            return m_present_queues.at(index);
        }

        [[nodiscard]] vk::PipelineCache get_pipeline_cache() const { return m_pipeline_cache; }

        [[nodiscard]] uint32_t get_memory_index(vk::MemoryPropertyFlags mem_props, uint32_t memory_type_bits) const;

    private:
        void create_pipeline_cache();

        vk::Device m_device;
        Context* m_context;
        VkSettings m_settings;

        std::vector<std::shared_ptr<Queue>> m_graphics_queues;
        std::vector<std::shared_ptr<Queue>> m_present_queues;
        vk::PipelineCache m_pipeline_cache;
    };
}
