#pragma once
#include "graphics/command_buffer.h"
#include "graphics/fence.h"
#include "graphics/semaphore.h"
#include <optional>
#include <vector>

namespace Comet {
    struct FrameSlot {
        Fence in_flight_fence;
        Semaphore image_available_semaphore;
        CommandBuffer command_buffer;

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

    class FrameManager {
    public:
        explicit FrameManager(Device& device, uint32_t frame_slot_count);

        void begin_frame() const;

        void prepare_image(uint32_t image_index);

        void end_frame();

        void initialize_swapchain_images(uint32_t image_count);

        [[nodiscard]] uint32_t get_current_frame_slot_index() const { return m_current_frame_slot; }
        [[nodiscard]] uint32_t get_frame_slot_count() const { return m_frame_slot_count; }

        [[nodiscard]] FrameSlot& get_current_frame_slot() {
            return m_frame_slots.at(m_current_frame_slot);
        }

        [[nodiscard]] SwapchainImageState& get_swapchain_image_state(const uint32_t image_index) {
            return m_swapchain_image_states.at(image_index);
        }

        [[nodiscard]] CommandBuffer& get_current_command_buffer() {
            return get_current_frame_slot().command_buffer;
        }

    private:
        Device& m_device;
        std::vector<FrameSlot> m_frame_slots;
        std::vector<SwapchainImageState> m_swapchain_image_states;
        uint32_t m_current_frame_slot = 0;
        uint32_t m_frame_slot_count = 0;
    };
}
