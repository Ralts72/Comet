#pragma once
#include <vulkan/vulkan.hpp>

namespace Comet {
    class Context;

    struct VkSettings {

    };
    class Device {
    public:
        Device(Context* context, uint32_t graphics_queue_count, uint32_t present_queue_count, const VkSettings& settings = {});
        ~Device();
    private:
        vk::Device m_device;
        Context* m_context;
        VkSettings m_settings;
    };
}