#pragma once
#include "vk_common.h"

namespace Comet {
    class Device;

    class CommandPool {
    public:
        CommandPool(Device* device, uint32_t queue_family_index);
        ~CommandPool();

        [[nodiscard]] std::vector<vk::CommandBuffer> allocate_command_buffers(uint32_t count) const;
        [[nodiscard]] vk::CommandBuffer allocate_command_buffer() const;
        [[nodiscard]] vk::CommandPool get_command_pool() const { return m_command_pool; }

    private:
        Device* m_device;
        vk::CommandPool m_command_pool;
    };

}
