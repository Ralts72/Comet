#include "frame_buffer.h"
#include "image_view.h"
#include "device.h"
#include "image.h"
#include "render_pass.h"

namespace Comet {
    FrameBuffer::FrameBuffer(Device* device, RenderPass* render_pass,
        const std::vector<std::shared_ptr<Image>>& images, const uint32_t width, const uint32_t height)
    : m_device(device), m_render_pass(render_pass), m_width(width), m_height(height) {
        recreate(images, width, height);
    }

    FrameBuffer::~FrameBuffer() {
        m_device->get_device().destroyFramebuffer(m_frame_buffer);
        m_image_views.clear();
    }

    bool FrameBuffer::recreate(const std::vector<std::shared_ptr<Image>>& images, const uint32_t width, const uint32_t height) {
        if (m_frame_buffer) {
            m_device->get_device().destroyFramebuffer(m_frame_buffer);
        }
        m_width = width;
        m_height = height;
        m_image_views.clear();

        std::vector<vk::ImageView> image_views;
        for(const auto& image : images) {
            //todo stencil format need to fix
            const bool is_depth_stencil = is_depth_stencil_format(image->get_info().format);
            vk::ImageAspectFlags aspect = is_depth_stencil ? vk::ImageAspectFlagBits::eDepth : vk::ImageAspectFlagBits::eColor;
            auto image_view = std::make_shared<ImageView>(m_device, *image, aspect);
            m_image_views.push_back(image_view);
            image_views.emplace_back(image_view->get_image_view());
        }

        vk::FramebufferCreateInfo create_info = {};
        create_info.renderPass = m_render_pass->get_render_pass();
        create_info.attachmentCount = static_cast<uint32_t>(image_views.size());
        create_info.pAttachments = image_views.data();
        create_info.width = m_width;
        create_info.height = m_height;
        create_info.layers = 1;
        m_frame_buffer = m_device->get_device().createFramebuffer(create_info);
        LOG_INFO("Vulkan framebuffer created successfully");
        LOG_TRACE("Framebuffer created, width: {} height: {}  view count: {}", m_width, m_height, m_image_views.size());
        return true;
    }
}
