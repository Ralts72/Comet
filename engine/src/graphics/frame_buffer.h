#pragma once
#include "vk_common.h"

namespace Comet {
    class Device;
    class RenderPass;
    class ImageView;

    class FrameBuffer {
    public:
        FrameBuffer(Device* device, RenderPass* render_pass, const std::vector<std::shared_ptr<ImageView>>& image_views, uint32_t width, uint32_t height);
        ~FrameBuffer();

        bool recreate(const std::vector<std::shared_ptr<ImageView>>& image_views, uint32_t width, uint32_t height);
        [[nodiscard]] vk::Framebuffer get() const { return m_frame_buffer; }
        [[nodiscard]] uint32_t get_width() const { return m_width; }
        [[nodiscard]] uint32_t get_height() const { return m_height; }
    private:
        vk::Framebuffer m_frame_buffer;
        Device* m_device;
        RenderPass* m_render_pass;

        uint32_t m_width, m_height;
    };
}