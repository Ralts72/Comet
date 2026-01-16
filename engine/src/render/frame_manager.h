#pragma once
#include "graphics/command_buffer.h"
#include "graphics/fence.h"
#include "graphics/semaphore.h"

namespace Comet {
    struct FrameSynchronization {
        Fence fence;
        Semaphore image_semaphore;
        Semaphore submit_semaphore;

        explicit FrameSynchronization(Device* device)
            : fence(device), image_semaphore(device), submit_semaphore(device) {}
    };

    class FrameManager {
    public:
        explicit FrameManager(Device* device, uint32_t frame_count);

        void begin_frame() const;
        void end_frame();

        [[nodiscard]] uint32_t get_current_frame() const { return m_current_frame; }
        [[nodiscard]] FrameSynchronization& get_current_sync() { return m_frame_syncs[m_current_frame]; }
        [[nodiscard]] CommandBuffer& get_command_buffer(const uint32_t image_index) { return m_command_buffers[image_index]; }
        [[nodiscard]] const std::vector<CommandBuffer>& get_command_buffers() const { return m_command_buffers; }

        void initialize_command_buffers(uint32_t count);

    private:
        Device* m_device;
        std::vector<FrameSynchronization> m_frame_syncs;
        std::vector<CommandBuffer> m_command_buffers;
        uint32_t m_current_frame = 0;
        uint32_t m_frame_count = 0;
    };
}

