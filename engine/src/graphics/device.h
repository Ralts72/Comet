#pragma once
#include "vk_common.h"

namespace Comet {
    class Context;
    class Queue;
    class Fence;
    class CommandPool;
    class CommandBuffer;

    struct VkSettings {
        vk::Format surface_format = vk::Format::eB8G8R8A8Unorm;
        vk::ColorSpaceKHR color_space = vk::ColorSpaceKHR::eSrgbNonlinear;
        vk::Format depth_format = vk::Format::eD32Sfloat;
        vk::PresentModeKHR present_mode = vk::PresentModeKHR::eImmediate;
        uint32_t swapchain_image_count = 3;
        vk::SampleCountFlagBits msaa_samples = vk::SampleCountFlagBits::e1;
    };

    class Device {
    public:
        Device(Context* context, uint32_t graphics_queue_count, uint32_t present_queue_count, const VkSettings& settings = {});

        ~Device();

        void wait_for_fences(std::span<const Fence> fences, bool wait_all = true,
                             uint64_t timeout = std::numeric_limits<uint64_t>::max()) const;

        void reset_fences(std::span<const Fence> fences) const;

        void wait_idle();

        void copy_buffer(vk::Buffer src, vk::Buffer dst, vk::DeviceSize size);

        void copy_buffer_to_image(vk::Buffer src, vk::Image dst_image, vk::ImageLayout dst_image_layout,
            const vk::Extent3D& extent, uint32_t base_array_layer = 0, uint32_t layer_count = 1, uint32_t mip_level = 0);
        void transition_image_layout(vk::Image image, vk::Format format, vk::ImageLayout old_layout, vk::ImageLayout new_layout,
            uint32_t base_array_layer = 0, uint32_t layer_count = 1, uint32_t mip_level = 0);

        [[nodiscard]] vk::Device get() const { return m_device; }
        [[nodiscard]] const VkSettings& get_settings() const { return m_settings; }

        [[nodiscard]] std::shared_ptr<Queue> get_graphics_queue(const uint32_t index = 0) const {
            return m_graphics_queues.at(index);
        }

        [[nodiscard]] std::shared_ptr<Queue> get_present_queue(const uint32_t index = 0) const {
            return m_present_queues.at(index);
        }

        [[nodiscard]] vk::PipelineCache get_pipeline_cache() const { return m_pipeline_cache; }

        [[nodiscard]] uint32_t get_memory_index(vk::MemoryPropertyFlags mem_props, uint32_t memory_type_bits) const;

        [[nodiscard]] std::shared_ptr<CommandPool> get_default_command_pool() const { return m_default_command_pool;}

    private:
        void create_pipeline_cache();
        void create_default_command_pool();
        void one_time_submit(const std::function<void(CommandBuffer)>& cmd_func);

        vk::Device m_device;
        Context* m_context;
        VkSettings m_settings;

        std::vector<std::shared_ptr<Queue>> m_graphics_queues;
        std::vector<std::shared_ptr<Queue>> m_present_queues;
        vk::PipelineCache m_pipeline_cache;
        std::shared_ptr<CommandPool> m_default_command_pool;
    };
}
