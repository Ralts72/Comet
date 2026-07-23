#include "frame_manager.h"
#include "common/logger.h"
#include "graphics/device.h"

namespace Comet {
    FrameManager::FrameManager(Device* device, const uint32_t frame_count)
        : m_device(device), m_frame_count(frame_count) {
        if(!device) {
            LOG_FATAL("FrameManager requires a valid Device");
        }
        if(frame_count == 0) {
            LOG_FATAL("FrameManager requires at least one frame in flight");
        }

        LOG_INFO("create command buffers");
        // Command buffers will be allocated when swapchain is created
        // This will be initialized later

        LOG_INFO("create fence and semaphore");
        for(uint32_t i = 0; i < frame_count; ++i) {
            m_frame_syncs.emplace_back(device);
        }
    }

    void FrameManager::begin_frame() const {
        const auto& fence = m_frame_syncs.at(m_current_frame).fence;
        m_device->wait_for_fences(std::span(&fence, 1));
    }

    void FrameManager::prepare_image(const uint32_t image_index) {
        if(const auto previous_frame = m_image_frames.at(image_index); previous_frame.has_value()) {
            const auto& previous_fence = m_frame_syncs.at(*previous_frame).fence;
            m_device->wait_for_fences(std::span(&previous_fence, 1));
        }

        auto& current_fence = m_frame_syncs.at(m_current_frame).fence;
        m_device->reset_fences(std::span(&current_fence, 1));
        m_image_frames.at(image_index) = m_current_frame;
    }

    void FrameManager::end_frame() {
        m_current_frame = (m_current_frame + 1) % m_frame_count;
    }

    void FrameManager::initialize_command_buffers(const uint32_t count) {
        m_image_frames.assign(count, std::nullopt);
        if(m_command_buffers.size() == count) {
            return;
        }
        if(!m_command_buffers.empty()) {
            m_device->get_default_command_pool().free_command_buffers(m_command_buffers);
            m_command_buffers.clear();
        }
        if(count == 0) {
            return;
        }
        m_command_buffers = m_device->get_default_command_pool().allocate_command_buffers(count);
    }
}
