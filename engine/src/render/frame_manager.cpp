#include "frame_manager.h"
#include "common/logger.h"
#include "graphics/device.h"

namespace Comet {
    FrameManager::FrameManager(Device* device, const uint32_t frame_count)
        : m_device(device), m_frame_count(frame_count) {
        LOG_INFO("create command buffers");
        // Command buffers will be allocated when swapchain is created
        // This will be initialized later

        LOG_INFO("create fence and semaphore");
        for(uint32_t i = 0; i < frame_count; ++i) {
            m_frame_syncs.emplace_back(device);
        }
    }

    void FrameManager::begin_frame() const {
        // Wait for fence
        const auto& fence = m_frame_syncs[m_current_frame].fence;
        m_device->wait_for_fences(std::span(&fence, 1));
        m_device->reset_fences(std::span(&fence, 1));
    }

    void FrameManager::end_frame() {
        m_current_frame = (m_current_frame + 1) % m_frame_count;
    }

    void FrameManager::initialize_command_buffers(const uint32_t count) {
        if (m_command_buffers.empty()) {
            m_command_buffers = m_device->get_default_command_pool().allocate_command_buffers(count);
        }
    }
}

