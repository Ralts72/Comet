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

    std::unique_ptr<RenderTarget> RenderTarget::create_offscreen_target(Device* device, RenderPass* render_pass, Math::Vec2u size) {
        return std::make_unique<OffscreenTarget>(device, render_pass, size);
    }

    std::unique_ptr<RenderTarget> RenderTarget::create_multi_target(Device* device, RenderPass* render_pass, Math::Vec2u size, uint32_t frame_count) {
        return std::make_unique<MultiTarget>(device, render_pass, size, frame_count);
    }

    // RenderTarget
    void RenderTarget::resize(const uint32_t width, const uint32_t height) {
        m_extent.x = width;
        m_extent.y = height;
        m_needs_recreate = true;
    }

    void RenderTarget::set_frame_count(const uint32_t frame_count) {
        m_frame_count = frame_count;
        m_needs_recreate = true;
    }

    void RenderTarget::set_clear_value(const ClearValue& clear_value, const int index) {
        const auto attachments = m_render_pass->get_attachments();
        if(index < 0) {
            for(size_t i = 0; i < attachments.size(); ++i) {
                set_clear_value(clear_value, i);
            }
            return;
        }
        if(static_cast<size_t>(index) >= attachments.size()) return;
        const auto& description = attachments[index].description;
        if(description.load_op != AttachmentLoadOp::Clear) return;

        const bool is_depth_stencil = Graphics::is_depth_stencil_format(description.format);
        if((clear_value.is_color() && !is_depth_stencil) ||
           (clear_value.is_depth_stencil() && is_depth_stencil)) {
            m_clear_values[index] = clear_value;
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
        : RenderTarget(device, render_pass, Math::Vec2u(swapchain->get_width(),
              swapchain->get_height()), swapchain->get_images().size()), m_swapchain(swapchain) {
        m_clear_values.resize(m_render_pass->get_attachments().size());
        set_clear_value(ClearValue(Math::Vec4(0.2f, 0.3f, 0.3f, 1.0f)));
        set_clear_value(ClearValue(1.0f, 0));
        recreate();
    }

    SwapchainTarget::~SwapchainTarget() {
        clear_render_resources(m_render_resources);
    }

    void SwapchainTarget::recreate() {
        if(m_extent.x == 0 || m_extent.y == 0) {
            return;
        }
        m_render_resources.clear();
        m_render_resources.resize(m_frame_count);

        const auto attachments = m_render_pass->get_attachments();
        if(attachments.empty()) {
            return;
        }

        for(uint32_t i = 0; i < m_frame_count; ++i) {
            std::vector<std::shared_ptr<ImageView>> all_views;
            std::vector<std::shared_ptr<Image>> color_images;
            std::vector<std::shared_ptr<ImageView>> color_views;
            std::shared_ptr<Image> depth_image = nullptr;
            std::shared_ptr<ImageView> depth_view = nullptr;

            for(const auto& [description, usage]: attachments) {
                ImageInfo image_info = {};
                image_info.format = description.format;
                image_info.extent = {m_extent.x, m_extent.y, 1};
                image_info.usage = usage;

                if(Graphics::is_depth_stencil_format(description.format)) {
                    depth_image = Image::create(m_device, image_info, description.samples);
                    depth_view = std::make_shared<ImageView>(m_device, *depth_image, Flags<ImageAspect>(ImageAspect::Depth));
                    all_views.push_back(depth_view);
                } else {
                    std::shared_ptr<Image> color_image;
                    if(description.final_layout == ImageLayout::PresentSrcKHR && description.samples == SampleCount::Count1) {
                        color_image = Image::wrap(m_device, m_swapchain->get_images()[i].get(), image_info);
                    } else {
                        color_image = Image::create(m_device, image_info, description.samples);
                    }
                    color_images.emplace_back(color_image);
                    auto color_view = std::make_shared<ImageView>(m_device, *color_image, Flags<ImageAspect>(ImageAspect::Color));
                    color_views.emplace_back(color_view);
                    all_views.push_back(color_view);
                }
            }

            m_render_resources[i].color_images = color_images;
            m_render_resources[i].color_views = color_views;
            if(depth_image) {
                m_render_resources[i].depth_image = depth_image;
                m_render_resources[i].depth_view = depth_view;
            }

            m_render_resources[i].frame_buffer = std::make_shared<FrameBuffer>(m_device, m_render_pass, all_views,
                m_extent.x, m_extent.y);
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

    OffscreenTarget::OffscreenTarget(Device* device, RenderPass* render_pass, const Math::Vec2u size) : RenderTarget(device, render_pass, size, 1) {
        m_clear_values.resize(m_render_pass->get_attachments().size());
        set_clear_value(ClearValue(Math::Vec4(0.2f, 0.3f, 0.3f, 1.0f)));
        set_clear_value(ClearValue(1.0f, 0));
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
        if(m_extent.x == 0 || m_extent.y == 0) {
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
                .extent = {m_extent.x, m_extent.y, 1},
                .usage = usage
            };

            if(Graphics::is_depth_stencil_format(description.format)) {
                m_depth_image = Image::create(m_device, image_info, description.samples);
                m_depth_view = std::make_shared<ImageView>(m_device, *m_depth_image, Flags<ImageAspect>(ImageAspect::Depth));
                all_views.emplace_back(m_depth_view);
            } else {
                if(!m_color_image) {
                    m_color_image = Image::create(m_device, image_info, description.samples);
                    m_color_view = std::make_shared<ImageView>(m_device, *m_color_image, Flags<ImageAspect>(ImageAspect::Color));
                    all_views.emplace_back(m_color_view);
                }
            }
        }

        m_frame_buffer = std::make_shared<FrameBuffer>(m_device, m_render_pass, all_views,
            m_extent.x, m_extent.y);
    }

    MultiTarget::MultiTarget(Device* device, RenderPass* render_pass, Math::Vec2u size, uint32_t frame_count) : RenderTarget(device, render_pass, size, frame_count) {
        m_clear_values.resize(m_render_pass->get_attachments().size());
        set_clear_value(ClearValue(Math::Vec4(0.2f, 0.3f, 0.3f, 1.0f)));
        set_clear_value(ClearValue(1.0f, 0));
        recreate();
    }

    MultiTarget::~MultiTarget() {
        clear_render_resources(m_render_resources);
    }

    void MultiTarget::recreate() {
        if(m_extent.x == 0 || m_extent.y == 0) {
            return;
        }
        m_render_resources.clear();
        m_render_resources.resize(m_frame_count);

        const auto attachments = m_render_pass->get_attachments();
        if(attachments.empty()) {
            return;
        }

        for(uint32_t i = 0; i < m_frame_count; ++i) {
            std::vector<std::shared_ptr<ImageView>> all_views;
            std::vector<std::shared_ptr<Image>> color_images;
            std::vector<std::shared_ptr<ImageView>> color_views;
            std::shared_ptr<Image> depth_image = nullptr;
            std::shared_ptr<ImageView> depth_view = nullptr;

            for(const auto& [description, usage]: attachments) {
                ImageInfo image_info = {
                    .format = description.format,
                    .extent = {m_extent.x, m_extent.y, 1},
                    .usage = usage
                };

                if(Graphics::is_depth_stencil_format(description.format)) {
                    depth_image = Image::create(m_device, image_info, description.samples);
                    depth_view = std::make_shared<ImageView>(m_device, *depth_image, Flags<ImageAspect>(ImageAspect::Depth));
                    all_views.push_back(depth_view);
                } else {
                    auto color_image = Image::create(m_device, image_info, description.samples);
                    color_images.emplace_back(color_image);
                    auto color_view = std::make_shared<ImageView>(m_device, *color_image, Flags<ImageAspect>(ImageAspect::Color));
                    color_views.emplace_back(color_view);
                    all_views.push_back(color_view);
                }
            }

            m_render_resources[i].color_images = color_images;
            m_render_resources[i].color_views = color_views;
            if(depth_image) {
                m_render_resources[i].depth_image = depth_image;
                m_render_resources[i].depth_view = depth_view;
            }

            m_render_resources[i].frame_buffer = std::make_shared<FrameBuffer>(m_device, m_render_pass, all_views,
                m_extent.x, m_extent.y);
        }
    }
}
