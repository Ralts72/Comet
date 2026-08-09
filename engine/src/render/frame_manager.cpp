#include "frame_manager.h"
#include "common/logger.h"
#include "graphics/device.h"

namespace Comet {
    FrameManager::FrameManager(Device& device, const uint32_t frame_slot_count)
        : m_device(device), m_frame_slot_count(frame_slot_count) {
        if(frame_slot_count == 0) {
            LOG_FATAL("FrameManager requires at least one frame slot");
        }

        LOG_INFO("create {} frame slots", frame_slot_count);
        const auto command_buffers =
            device.get_default_command_pool().allocate_command_buffers(frame_slot_count);
        m_frame_slots.reserve(frame_slot_count);
        for(uint32_t i = 0; i < frame_slot_count; ++i) {
            m_frame_slots.emplace_back(device, command_buffers.at(i));
        }
    }

    void FrameManager::begin_frame() const {
        const auto& fence = m_frame_slots.at(m_current_frame_slot).in_flight_fence;
        m_device.wait_for_fences(std::span(&fence, 1));
    }

    void FrameManager::prepare_image(const uint32_t image_index) {
        auto& image_state = m_swapchain_image_states.at(image_index);
        if(const auto previous_frame_slot = image_state.in_flight_frame_slot;
           previous_frame_slot.has_value()) {
            const auto& previous_fence =
                m_frame_slots.at(*previous_frame_slot).in_flight_fence;
            m_device.wait_for_fences(std::span(&previous_fence, 1));
        }

        auto& current_fence =
            m_frame_slots.at(m_current_frame_slot).in_flight_fence;
        m_device.reset_fences(std::span(&current_fence, 1));
        image_state.in_flight_frame_slot = m_current_frame_slot;
    }

    void FrameManager::end_frame() {
        m_current_frame_slot =
            (m_current_frame_slot + 1) % m_frame_slot_count;
    }

    void FrameManager::initialize_swapchain_images(const uint32_t image_count) {
        if(image_count == 0) {
            LOG_FATAL("FrameManager requires at least one swapchain image");
        }

        LOG_INFO("create {} swapchain image states for {} frame slots",
            image_count, m_frame_slot_count);
        m_swapchain_image_states.clear();
        m_swapchain_image_states.reserve(image_count);
        for(uint32_t i = 0; i < image_count; ++i) {
            m_swapchain_image_states.emplace_back(m_device);
        }
    }
}
