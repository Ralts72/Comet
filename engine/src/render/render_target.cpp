#include "render_target.h"
#include "graphics/command_buffer.h"
#include "graphics/swapchain.h"
#include "graphics/image.h"
#include "graphics/render_pass.h"
#include "graphics/image_view.h"
#include "graphics/frame_buffer.h"

namespace Comet {
    std::unique_ptr<RenderTarget> RenderTarget::create_swapchain_target(Device* device, RenderPass* render_pass, Swapchain* swapchain) {
        return std::make_unique<SwapchainTarget>(device, render_pass, swapchain);
    }

    std::unique_ptr<RenderTarget> RenderTarget::create_offscreen_target(Device* device, RenderPass* render_pass, Math::Vec2i size) {
        return std::make_unique<OffscreenTarget>(device, render_pass, size);
    }

    std::unique_ptr<RenderTarget> RenderTarget::create_multi_target(Device* device, RenderPass* render_pass, Math::Vec2i size, uint32_t frame_count) {
        return std::make_unique<MultiTarget>(device, render_pass, size, frame_count);
    }

    // RenderTarget
    void RenderTarget::resize(const uint32_t width, const uint32_t height) {
        m_extent.width = width;
        m_extent.height = height;
        m_needs_recreate = true;
    }

    void RenderTarget::set_frame_count(const uint32_t frame_count) {
        m_frame_count = frame_count;
        m_needs_recreate = true;
    }

    void RenderTarget::set_color_clear_value(const Math::Vec4 color) {
        const auto clear_value = vk::ClearColorValue(std::array<float, 4>({color.x, color.y, color.z, color.w}));
        const auto attachments = m_render_pass->get_attachments();
        for(uint32_t i = 0; i < attachments.size(); ++i) {
            const auto description = attachments[i].description;
            if(!is_depth_stencil_format(description.format) && description.loadOp == vk::AttachmentLoadOp::eClear) {
                m_clear_values[i] = clear_value;
            }
        }
    }

    void RenderTarget::set_color_clear_value(const uint32_t index, const Math::Vec4 color) {
        const auto clear_value = vk::ClearColorValue(std::array<float, 4>({color.x, color.y, color.z, color.w}));
        const auto attachments = m_render_pass->get_attachments();
        if(index >= attachments.size()) {
            return;
        }
        const auto description = attachments[index].description;
        if(!is_depth_stencil_format(description.format) && description.loadOp == vk::AttachmentLoadOp::eClear) {
            m_clear_values[index] = clear_value;
        }
    }

    void RenderTarget::set_depth_stencil_clear_value(const Math::Vec2 depth_stencil) {
        const auto depth_stencil_value = vk::ClearDepthStencilValue(depth_stencil.x, static_cast<int>(depth_stencil.y));
        const auto attachments = m_render_pass->get_attachments();
        for(uint32_t i = 0; i < attachments.size(); ++i) {
            const auto description = attachments[i].description;
            if(is_depth_stencil_format(description.format) && description.loadOp == vk::AttachmentLoadOp::eClear) {
                m_clear_values[i] = depth_stencil_value;
            }
        }
    }

    void RenderTarget::set_depth_stencil_clear_value(const uint32_t index, const Math::Vec2 depth_stencil) {
        const auto depth_stencil_value = vk::ClearDepthStencilValue(depth_stencil.x, static_cast<int>(depth_stencil.y));
        const auto attachments = m_render_pass->get_attachments();
        if(index >= attachments.size()) {
            return;
        }
        const auto description = attachments[index].description;
        if(is_depth_stencil_format(description.format) && description.loadOp == vk::AttachmentLoadOp::eClear) {
            m_clear_values[index] = depth_stencil_value;
        }
    }

    void RenderTarget::begin_render_target(CommandBuffer& command_buffer) {
        if(m_needs_recreate) {
            recreate();
            m_needs_recreate = false;
        }
        m_current_image_index = (m_current_image_index + 1) % m_frame_count;
        command_buffer.begin_render_pass(*m_render_pass, *get_framebuffer(m_current_image_index), m_clear_values);
    }

    void RenderTarget::end_render_target(CommandBuffer& command_buffer) {
        command_buffer.end_render_pass();
    }

    void RenderTarget::clear_render_resources(std::vector<RenderResource>& resources) {
        for(auto& resource: resources) {
            resource.frame_buffer.reset();
            resource.color_views.clear();
            resource.depth_view.reset();
            resource.color_images.clear();
            resource.depth_image.reset();
        }
        resources.clear();
    }

    // SwapchainTarget
    SwapchainTarget::SwapchainTarget(Device* device, RenderPass* render_pass, Swapchain* swapchain)
        : RenderTarget(device, render_pass, RenderTargetType::Swapchain, Math::Vec2i(swapchain->get_width(),
              swapchain->get_height()), swapchain->get_images().size()), m_swapchain(swapchain) {
        m_clear_values.resize(m_render_pass->get_attachments().size());
        set_color_clear_value(Math::Vec4(0.2f, 0.3f, 0.3f, 1.0f));
        set_depth_stencil_clear_value(Math::Vec2(1.0f, 0.0f));
        recreate();
    }

    SwapchainTarget::~SwapchainTarget() {
        clear_render_resources(m_render_resources);
    }

    void SwapchainTarget::recreate() {
        if(m_extent.width == 0 || m_extent.height == 0) {
            return;
        }
        m_render_resources.clear();
        m_render_resources.resize(m_frame_count);

        const auto attachments = m_render_pass->get_attachments();
        if(attachments.empty()) {
            return;
        }

        for(uint32_t i = 0; i < m_frame_count; ++i) {
            std::vector<std::shared_ptr<Image>> color_images;
            std::vector<std::shared_ptr<ImageView>> color_views;
            std::shared_ptr<Image> depth_image = nullptr;
            std::shared_ptr<ImageView> depth_view = nullptr;

            for(const auto& [description, usage]: attachments) {
                ImageInfo image_info = {
                    .format = description.format,
                    .extent = {m_extent.width, m_extent.height, 1},
                    .usage = usage
                };

                if(is_depth_stencil_format(description.format)) {
                    depth_image = Image::create_owned_image(m_device, image_info);
                    depth_view = std::make_shared<ImageView>(m_device, *depth_image, vk::ImageAspectFlagBits::eDepth);
                } else {
                    std::shared_ptr<Image> color_image;
                    if(description.finalLayout == vk::ImageLayout::ePresentSrcKHR && description.samples == vk::SampleCountFlagBits::e1) {
                        color_image = Image::create_borrowed_image(m_device, m_swapchain->get_images()[i].get(), image_info);
                    } else {
                        color_image = Image::create_owned_image(m_device, image_info);
                    }
                    color_images.emplace_back(color_image);
                    color_views.emplace_back(std::make_shared<ImageView>(m_device, *color_image, vk::ImageAspectFlagBits::eColor));
                }
            }

            m_render_resources[i].color_images = color_images;
            m_render_resources[i].color_views = color_views;
            if(depth_image) {
                m_render_resources[i].depth_image = depth_image;
                m_render_resources[i].depth_view = depth_view;
            }

            std::vector<std::shared_ptr<ImageView>> all_views = color_views;
            if(depth_view) {
                all_views.emplace_back(depth_view);
            }

            m_render_resources[i].frame_buffer = std::make_shared<FrameBuffer>(m_device, m_render_pass, all_views,
                m_extent.width, m_extent.height);
        }
    }

    void SwapchainTarget::begin_render_target(CommandBuffer& command_buffer) {
        if(m_needs_recreate) {
            recreate();
            m_needs_recreate = false;
        }
        m_current_image_index = m_swapchain->get_current_index();

        command_buffer.begin_render_pass(*m_render_pass, *get_framebuffer(m_current_image_index), m_clear_values);
    }

    OffscreenTarget::OffscreenTarget(Device* device, RenderPass* render_pass, Math::Vec2i size) : RenderTarget(device, render_pass, RenderTargetType::Offscreen, size, 1) {
        m_clear_values.resize(m_render_pass->get_attachments().size());
        set_color_clear_value(Math::Vec4(0.2f, 0.3f, 0.3f, 1.0f)); // 深蓝灰色背景
        set_depth_stencil_clear_value(Math::Vec2(1.0f, 0.0f));
        recreate();
    }

    OffscreenTarget::~OffscreenTarget() {
        m_frame_buffer.reset();
        m_color_view.reset();
        m_depth_view.reset();
        m_color_image.reset();
        m_depth_image.reset();
    }

    void OffscreenTarget::recreate() {
        if(m_extent.width == 0 || m_extent.height == 0) {
            return;
        }

        m_frame_buffer.reset();
        m_color_view.reset();
        m_depth_view.reset();
        m_color_image.reset();
        m_depth_image.reset();

        const auto attachments = m_render_pass->get_attachments();
        if(attachments.empty()) {
            return;
        }

        std::vector<std::shared_ptr<ImageView>> all_views;

        for(const auto& [description, usage]: attachments) {
            ImageInfo image_info = {
                .format = description.format,
                .extent = {m_extent.width, m_extent.height, 1},
                .usage = usage
            };

            if(is_depth_stencil_format(description.format)) {
                m_depth_image = Image::create_owned_image(m_device, image_info);
                m_depth_view = std::make_shared<ImageView>(m_device, *m_depth_image, vk::ImageAspectFlagBits::eDepth);
                all_views.emplace_back(m_depth_view);
            } else {
                if(!m_color_image) {
                    m_color_image = Image::create_owned_image(m_device, image_info);
                    m_color_view = std::make_shared<ImageView>(m_device, *m_color_image, vk::ImageAspectFlagBits::eColor);
                    all_views.emplace_back(m_color_view);
                }
            }
        }

        m_frame_buffer = std::make_shared<FrameBuffer>(m_device, m_render_pass, all_views,
            m_extent.width, m_extent.height);
    }

    MultiTarget::MultiTarget(Device* device, RenderPass* render_pass, Math::Vec2i size, uint32_t frame_count) : RenderTarget(device, render_pass, RenderTargetType::MultiAttachment, size, frame_count) {
        m_clear_values.resize(m_render_pass->get_attachments().size());
        set_color_clear_value(Math::Vec4(0.2f, 0.3f, 0.3f, 1.0f)); // 深蓝灰色背景
        set_depth_stencil_clear_value(Math::Vec2(1.0f, 0.0f));
        recreate();
    }

    MultiTarget::~MultiTarget() {
        clear_render_resources(m_render_resources);
    }

    void MultiTarget::recreate() {
        if(m_extent.width == 0 || m_extent.height == 0) {
            return;
        }
        m_render_resources.clear();
        m_render_resources.resize(m_frame_count);

        const auto attachments = m_render_pass->get_attachments();
        if(attachments.empty()) {
            return;
        }

        for(uint32_t i = 0; i < m_frame_count; ++i) {
            std::vector<std::shared_ptr<Image>> color_images;
            std::vector<std::shared_ptr<ImageView>> color_views;
            std::shared_ptr<Image> depth_image = nullptr;
            std::shared_ptr<ImageView> depth_view = nullptr;

            for(const auto& [description, usage]: attachments) {
                ImageInfo image_info = {
                    .format = description.format,
                    .extent = {m_extent.width, m_extent.height, 1},
                    .usage = usage
                };

                if(is_depth_stencil_format(description.format)) {
                    depth_image = Image::create_owned_image(m_device, image_info);
                    depth_view = std::make_shared<ImageView>(m_device, *depth_image, vk::ImageAspectFlagBits::eDepth);
                } else {
                    auto color_image = Image::create_owned_image(m_device, image_info);
                    color_images.emplace_back(color_image);
                    color_views.emplace_back(std::make_shared<ImageView>(m_device, *color_image, vk::ImageAspectFlagBits::eColor));
                }
            }

            m_render_resources[i].color_images = color_images;
            m_render_resources[i].color_views = color_views;
            if(depth_image) {
                m_render_resources[i].depth_image = depth_image;
                m_render_resources[i].depth_view = depth_view;
            }

            std::vector<std::shared_ptr<ImageView>> all_views = color_views;
            if(depth_view) {
                all_views.emplace_back(depth_view);
            }

            m_render_resources[i].frame_buffer = std::make_shared<FrameBuffer>(m_device, m_render_pass, all_views,
                m_extent.width, m_extent.height);
        }
    }
}
