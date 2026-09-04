#include "frame_buffer.h"
#include "graphics/resource/image_view.h"
#include "device.h"
#include "render_pass.h"
#include "diagnostics/logger.h"

#include <utility>

namespace Comet {
    std::shared_ptr<FrameBuffer> FrameBuffer::create(Device& device,
        RenderPass& render_pass,
        const std::vector<std::shared_ptr<ImageView>>& image_views, const uint32_t width,
        const uint32_t height) {
        auto attempt = try_create(device, render_pass, image_views, width, height);
        if(!attempt) {
            LOG_FATAL(
                "Failed to create framebuffer: {}", vk::to_string(attempt.result()));
        }
        return std::move(attempt).value();
    }

    GpuResourceResult<std::shared_ptr<FrameBuffer>> FrameBuffer::try_create(
        Device& device, RenderPass& render_pass,
        const std::vector<std::shared_ptr<ImageView>>& image_views, const uint32_t width,
        const uint32_t height) {
        if(image_views.empty() || width == 0 || height == 0) {
            LOG_FATAL("Framebuffer requires attachments and a non-zero extent");
        }

        std::vector<vk::ImageView> vk_image_views;
        vk_image_views.reserve(image_views.size());
        for(const auto& image_view : image_views) {
            if(!image_view) {
                LOG_FATAL("Framebuffer attachments must not be null");
            }
            vk_image_views.emplace_back(image_view->get());
        }

        vk::FramebufferCreateInfo create_info{};
        create_info.renderPass = render_pass.get();
        create_info.attachmentCount = static_cast<uint32_t>(vk_image_views.size());
        create_info.pAttachments = vk_image_views.data();
        create_info.width = width;
        create_info.height = height;
        create_info.layers = 1;

        vk::Framebuffer frame_buffer;
        const vk::Result result =
            device.get().createFramebuffer(&create_info, nullptr, &frame_buffer);
        if(result != vk::Result::eSuccess) {
            return GpuResourceResult<std::shared_ptr<FrameBuffer>>::failure(result);
        }

        std::shared_ptr<FrameBuffer> owner(new FrameBuffer(
            device, render_pass, image_views, width, height, frame_buffer));
        LOG_INFO("Vulkan framebuffer created successfully");
        LOG_TRACE("Framebuffer created, width: {} height: {}  view count: {}", width,
            height, vk_image_views.size());
        return GpuResourceResult<std::shared_ptr<FrameBuffer>>::success(std::move(owner));
    }

    FrameBuffer::FrameBuffer(Device& device, RenderPass& render_pass,
        std::vector<std::shared_ptr<ImageView>> image_views, const uint32_t width,
        const uint32_t height, const vk::Framebuffer frame_buffer)
        : m_frame_buffer(frame_buffer), m_device(device), m_render_pass(render_pass),
          m_attachments(std::move(image_views)), m_width(width), m_height(height) {}

    FrameBuffer::~FrameBuffer() {
        if(m_frame_buffer) {
            m_device.get().destroyFramebuffer(m_frame_buffer);
        }
    }
}
