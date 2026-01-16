#include "frame_buffer.h"
#include "image_view.h"
#include "device.h"
#include "render_pass.h"

namespace Comet {
    FrameBuffer::FrameBuffer(Device* device, RenderPass* render_pass,
        const std::vector<std::shared_ptr<ImageView>>& image_views, const uint32_t width, const uint32_t height)
    : m_device(device), m_render_pass(render_pass), m_width(width), m_height(height) {
        recreate(image_views, width, height);
    }

    FrameBuffer::~FrameBuffer() {
        m_device->get().destroyFramebuffer(m_frame_buffer);
    }

    bool FrameBuffer::recreate(const std::vector<std::shared_ptr<ImageView>>& image_views, const uint32_t width, const uint32_t height) {
        if (m_frame_buffer) {
            m_device->get().destroyFramebuffer(m_frame_buffer);
        }
        m_width = width;
        m_height = height;

        std::vector<vk::ImageView> vk_image_views;
        vk_image_views.reserve(image_views.size());
        for(const auto& image_view : image_views) {
            vk_image_views.emplace_back(image_view->get());
        }

        vk::FramebufferCreateInfo create_info = {};
        create_info.renderPass = m_render_pass->get();
        create_info.attachmentCount = static_cast<uint32_t>(vk_image_views.size());
        create_info.pAttachments = vk_image_views.data();
        create_info.width = m_width;
        create_info.height = m_height;
        create_info.layers = 1;
        m_frame_buffer = m_device->get().createFramebuffer(create_info);
        LOG_INFO("Vulkan framebuffer created successfully");
        LOG_TRACE("Framebuffer created, width: {} height: {}  view count: {}", m_width, m_height, vk_image_views.size());
        return true;
    }
}
