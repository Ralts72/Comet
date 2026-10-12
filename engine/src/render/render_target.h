#pragma once
#include "graphics/vk_common.h"
#include "graphics/result.h"
#include "core/math_utils.h"
#include "common/export.h"

#include <cstddef>
#include <memory>
#include <vector>

namespace Comet {
    class ImageView;
    class FrameBuffer;
    class RenderPass;
    class Device;
    class CommandBuffer;
    class Swapchain;

    class COMET_API RenderTarget {
    public:
        static Result<std::unique_ptr<RenderTarget>, GraphicsError> create_swapchain_target(
            Device& device, RenderPass& render_pass, Swapchain& swapchain);

        [[nodiscard]] static GpuResourceResult<std::unique_ptr<RenderTarget>>
        try_create_multi_target(
            Device& device, RenderPass& render_pass, Math::Vec2u size, uint32_t frame_count);

        virtual ~RenderTarget() = default;

        void set_clear_value(const ClearValue& clear_value);

        void set_clear_value(const ClearValue& clear_value, std::size_t index);

        virtual void begin_render_target(CommandBuffer& command_buffer);

        void begin_render_target(const CommandBuffer& command_buffer, uint32_t frame_index);

        virtual void end_render_target(CommandBuffer& command_buffer);

        [[nodiscard]] Math::Vec2u get_size() const { return m_extent; }

        [[nodiscard]] virtual std::shared_ptr<FrameBuffer> get_framebuffer(
            uint32_t index) const = 0;

        [[nodiscard]] virtual std::shared_ptr<ImageView> get_color_view(uint32_t index) const = 0;

    protected:
        RenderTarget(Device& device, RenderPass& render_pass, const Math::Vec2u size,
            const uint32_t frame_count)
            : m_device(device), m_render_pass(render_pass), m_extent(size),
              m_frame_count(frame_count), m_clear_values({}), m_current_image_index(0) {}

        Device& m_device;
        RenderPass& m_render_pass;
        const Math::Vec2u m_extent;
        const uint32_t m_frame_count;
        std::vector<ClearValue> m_clear_values;
        uint32_t m_current_image_index;
    };

}
