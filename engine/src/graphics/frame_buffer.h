#pragma once
#include "vk_common.h"
#include "common/export.h"
#include "graphics/resource/resource_result.h"

#include <memory>
#include <vector>

namespace Comet {
    class Device;
    class RenderPass;
    class ImageView;

    class COMET_API FrameBuffer {
    public:
        [[nodiscard]] static std::shared_ptr<FrameBuffer> create(
            Device& device,
            RenderPass& render_pass,
            const std::vector<std::shared_ptr<ImageView>>& image_views,
            uint32_t width,
            uint32_t height);

        [[nodiscard]] static GpuResourceResult<std::shared_ptr<FrameBuffer>>
        try_create(
            Device& device,
            RenderPass& render_pass,
            const std::vector<std::shared_ptr<ImageView>>& image_views,
            uint32_t width,
            uint32_t height);

        ~FrameBuffer();

        FrameBuffer(const FrameBuffer&) = delete;
        FrameBuffer& operator=(const FrameBuffer&) = delete;
        FrameBuffer(FrameBuffer&&) noexcept = delete;
        FrameBuffer& operator=(FrameBuffer&&) noexcept = delete;

        [[nodiscard]] vk::Framebuffer get() const { return m_frame_buffer; }
        [[nodiscard]] uint32_t get_width() const { return m_width; }
        [[nodiscard]] uint32_t get_height() const { return m_height; }
    private:
        FrameBuffer(
            Device& device,
            RenderPass& render_pass,
            std::vector<std::shared_ptr<ImageView>> image_views,
            uint32_t width,
            uint32_t height,
            vk::Framebuffer frame_buffer);

        vk::Framebuffer m_frame_buffer = VK_NULL_HANDLE;
        Device& m_device;
        RenderPass& m_render_pass;
        std::vector<std::shared_ptr<ImageView>> m_attachments;

        uint32_t m_width, m_height;
    };
}
