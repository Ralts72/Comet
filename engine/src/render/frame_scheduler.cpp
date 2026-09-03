#include "render/frame_scheduler.h"

#include "diagnostics/logger.h"
#include "graphics/device.h"

#include <algorithm>
#include <limits>
#include <utility>

namespace Comet {
    FrameScheduler::FrameScheduler(
        Device& device, const uint32_t frame_slot_count)
        : m_device(device), m_frame_slot_count(frame_slot_count) {
        if(frame_slot_count == 0) {
            LOG_FATAL("FrameScheduler requires at least one frame slot");
        }

        LOG_INFO("create {} frame slots", frame_slot_count);
        const auto command_buffers =
            device.get_default_command_pool().allocate_command_buffers(
                frame_slot_count);
        m_frame_slots.reserve(frame_slot_count);
        for(uint32_t index = 0; index < frame_slot_count; ++index) {
            m_frame_slots.emplace_back(device, command_buffers.at(index));
        }
    }

    void FrameScheduler::wait_for_current_slot() {
        if(m_frame_active) {
            LOG_FATAL("Cannot wait for a frame slot while a frame is active");
        }
        if(m_current_slot_ready) {
            return;
        }

        wait_for_slot(m_current_frame_slot);
        m_current_slot_ready = true;
    }

    void FrameScheduler::wait_for_all_slots() {
        if(m_frame_active && !m_submission_recorded) {
            LOG_FATAL("Cannot wait for all frame slots before the active frame is submitted");
        }
        for(uint32_t frame_slot = 0;
            frame_slot < m_frame_slot_count;
            ++frame_slot) {
            wait_for_slot(frame_slot);
        }
        if(!m_frame_active) {
            m_current_slot_ready = true;
        }
    }

    void FrameScheduler::begin_frame(const uint32_t image_index) {
        if(!m_current_slot_ready || m_frame_active) {
            LOG_FATAL("FrameScheduler requires a ready inactive slot before begin_frame");
        }

        auto& image_state = m_swapchain_image_states.at(image_index);
        if(const auto previous_frame_slot = image_state.in_flight_frame_slot;
           previous_frame_slot.has_value()) {
            wait_for_slot(*previous_frame_slot);
        }

        auto& current_fence = get_current_frame_slot().in_flight_fence;
        m_device.reset_fences(std::span(&current_fence, 1));
        image_state.in_flight_frame_slot = m_current_frame_slot;
        m_frame_active = true;
        m_submission_recorded = false;
    }

    void FrameScheduler::retain_current_frame_resource(
        std::shared_ptr<void> resource) {
        if(!m_frame_active || m_submission_recorded) {
            LOG_FATAL("Frame resources can only be retained while recording an active frame");
        }
        if(!resource) {
            return;
        }

        const void* resource_id = resource.get();
        get_current_frame_slot().retained_resources.try_emplace(
            resource_id, std::move(resource));
    }

    void FrameScheduler::record_submission() {
        if(!m_frame_active || m_submission_recorded) {
            LOG_FATAL("FrameScheduler requires one submission for the active frame");
        }

        m_submission_recorded = true;
        get_current_frame_slot().last_submission_serial =
            m_current_frame_serial;
    }

    void FrameScheduler::end_frame() {
        if(!m_frame_active || !m_submission_recorded) {
            LOG_FATAL("Cannot end a frame before recording its submission");
        }
        if(m_current_frame_serial == std::numeric_limits<uint64_t>::max()) {
            LOG_FATAL("Frame submission serial exhausted");
        }

        ++m_current_frame_serial;
        m_current_frame_slot =
            (m_current_frame_slot + 1) % m_frame_slot_count;
        m_current_slot_ready = false;
        m_frame_active = false;
        m_submission_recorded = false;
    }

    void FrameScheduler::initialize_swapchain_images(
        const uint32_t image_count) {
        if(image_count == 0) {
            LOG_FATAL("FrameScheduler requires at least one swapchain image");
        }

        LOG_INFO("create {} swapchain image states for {} frame slots",
            image_count, m_frame_slot_count);
        m_swapchain_image_states.clear();
        m_swapchain_image_states.reserve(image_count);
        for(uint32_t index = 0; index < image_count; ++index) {
            m_swapchain_image_states.emplace_back(m_device);
        }
    }

    void FrameScheduler::wait_for_slot(const uint32_t frame_slot_index) {
        auto& slot = m_frame_slots.at(frame_slot_index);
        m_device.wait_for_fences(std::span(&slot.in_flight_fence, 1));
        slot.retained_resources.clear();
        m_completed_frame_serial = std::max(
            m_completed_frame_serial,
            slot.last_submission_serial);
    }
}
