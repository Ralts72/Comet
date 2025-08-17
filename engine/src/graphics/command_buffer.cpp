#include "command_buffer.h"
#include "device.h"
#include "core/logger/logger.h"

namespace Comet {
    CommandPool::CommandPool(Device* device, const uint32_t queue_family_index): m_device(device) {
        vk::CommandPoolCreateInfo pool_info = {};
        pool_info.flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer;
        pool_info.queueFamilyIndex = queue_family_index;
        m_command_pool = m_device->get_device().createCommandPool(pool_info);
        LOG_INFO("Vulkan command pool created successfully");
    }
    CommandPool::~CommandPool() {
        m_device->get_device().destroyCommandPool(m_command_pool);
    }

    std::vector<vk::CommandBuffer> CommandPool::allocate_command_buffers(const uint32_t count) const {
        std::vector<vk::CommandBuffer> cmd_buffers(count);
        vk::CommandBufferAllocateInfo allocate_info = {};
        allocate_info.commandPool = m_command_pool;
        allocate_info.commandBufferCount = count;
        allocate_info.level = vk::CommandBufferLevel::ePrimary;
        cmd_buffers = m_device->get_device().allocateCommandBuffers(allocate_info);
        LOG_INFO("Vulkan command buffers allocated successfully (count: {})", count);
        return cmd_buffers;
    }
    vk::CommandBuffer CommandPool::allocate_command_buffer() const {
        const auto buffer = allocate_command_buffers(1);
        return buffer[0];
    }

}
