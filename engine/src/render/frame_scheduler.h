#pragma once

#include "graphics/command/command_buffer.h"
#include "graphics/synchronization/fence.h"
#include "graphics/synchronization/semaphore.h"

#include <cstdint>
#include <vector>

namespace Comet {
    struct FrameSlot {
        Fence in_flight_fence;
        Semaphore image_available_semaphore;
        CommandBuffer command_buffer;
        uint64_t last_submission_serial = 0;

        FrameSlot(Device& device, const CommandBuffer& command_buffer)
            : in_flight_fence(device),
              image_available_semaphore(device),
              command_buffer(command_buffer) {}
    };

    struct SwapchainImageState {
        Semaphore render_finished_semaphore;
        std::optional<uint32_t> in_flight_frame_slot;

        explicit SwapchainImageState(Device& device)
            : render_finished_semaphore(device) {}
    };

    class FrameScheduler {
    public:
        explicit FrameScheduler(Device& device, uint32_t frame_slot_count);

        FrameScheduler(const FrameScheduler&) = delete;
        FrameScheduler& operator=(const FrameScheduler&) = delete;
        FrameScheduler(FrameScheduler&&) noexcept = delete;
        FrameScheduler& operator=(FrameScheduler&&) noexcept = delete;

        void wait_for_current_slot();
        void wait_for_all_slots();
        void begin_frame(uint32_t image_index);
        void record_submission();
        void end_frame();

        void initialize_swapchain_images(uint32_t image_count);

        [[nodiscard]] uint32_t get_current_frame_slot_index() const {
            return m_current_frame_slot;
        }
        [[nodiscard]] uint32_t get_frame_slot_count() const {
            return m_frame_slot_count;
        }
        [[nodiscard]] uint64_t get_current_frame_serial() const {
            return m_current_frame_serial;
        }
        [[nodiscard]] uint64_t get_completed_frame_serial() const {
            return m_completed_frame_serial;
        }
        [[nodiscard]] bool is_frame_serial_complete(
            const uint64_t frame_serial) const {
            return frame_serial == 0
                || frame_serial <= m_completed_frame_serial;
        }

        [[nodiscard]] FrameSlot& get_current_frame_slot() {
            return m_frame_slots.at(m_current_frame_slot);
        }
        [[nodiscard]] const FrameSlot& get_current_frame_slot() const {
            return m_frame_slots.at(m_current_frame_slot);
        }

        [[nodiscard]] SwapchainImageState& get_swapchain_image_state(
            uint32_t image_index) {
            return m_swapchain_image_states.at(image_index);
        }

        [[nodiscard]] CommandBuffer& get_current_command_buffer() {
            return get_current_frame_slot().command_buffer;
        }

    private:
        void wait_for_slot(uint32_t frame_slot_index);

        Device& m_device;
        std::vector<FrameSlot> m_frame_slots;
        std::vector<SwapchainImageState> m_swapchain_image_states;
        uint32_t m_current_frame_slot = 0;
        uint32_t m_frame_slot_count = 0;
        uint64_t m_current_frame_serial = 1;
        uint64_t m_completed_frame_serial = 0;
        bool m_current_slot_ready = false;
        bool m_frame_active = false;
        bool m_submission_recorded = false;
    };
}
