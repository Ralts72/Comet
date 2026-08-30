#include "render_target.h"
#include "graphics/command/command_buffer.h"
#include "graphics/swapchain.h"
#include "graphics/resource/image.h"
#include "graphics/render_pass.h"
#include "graphics/resource/image_view.h"
#include "graphics/frame_buffer.h"

#include <utility>

namespace Comet {
    namespace {
        GpuResourceResult<RenderResource> try_create_owned_render_resource(
            Device& device,
            RenderPass& render_pass,
            const Math::Vec2u extent) {
            std::vector<std::shared_ptr<ImageView>> all_views;
            std::vector<std::shared_ptr<ImageView>> color_views;

            for(const auto& [description, usage]: render_pass.get_attachments()) {
                const ImageInfo image_info = {
                    .format = description.format,
                    .extent = {extent.x, extent.y, 1},
                    .usage = usage
                };
                auto image_attempt = Image::try_create(
                    device,
                    image_info,
                    true,
                    description.samples,
                    Graphics::is_depth_stencil_format(description.format)
                        ? "render target depth image"
                        : "render target color image");
                if(!image_attempt) {
                    return GpuResourceResult<RenderResource>::failure(
                        image_attempt.result());
                }

                const bool is_depth =
                    Graphics::is_depth_stencil_format(description.format);
                auto view_attempt = ImageView::try_create(
                    device,
                    std::move(image_attempt).value(),
                    Flags<ImageAspect>(
                        is_depth ? ImageAspect::Depth : ImageAspect::Color));
                if(!view_attempt) {
                    return GpuResourceResult<RenderResource>::failure(
                        view_attempt.result());
                }

                auto view = std::move(view_attempt).value();
                if(!is_depth) {
                    color_views.push_back(view);
                }
                all_views.push_back(std::move(view));
            }

            auto frame_buffer_attempt = FrameBuffer::try_create(
                device,
                render_pass,
                all_views,
                extent.x,
                extent.y);
            if(!frame_buffer_attempt) {
                return GpuResourceResult<RenderResource>::failure(
                    frame_buffer_attempt.result());
            }

            return GpuResourceResult<RenderResource>::success({
                .color_views = std::move(color_views),
                .frame_buffer = std::move(frame_buffer_attempt).value()
            });
        }
    }

    std::unique_ptr<RenderTarget> RenderTarget::create_swapchain_target(
        Device& device,
        RenderPass& render_pass,
        std::shared_ptr<SwapchainGeneration> swapchain_generation) {
        if(!swapchain_generation) {
            LOG_FATAL("SwapchainTarget requires a valid swapchain generation");
        }
        return std::unique_ptr<RenderTarget>(new SwapchainTarget(
            device, render_pass, std::move(swapchain_generation)));
    }

    std::unique_ptr<RenderTarget> RenderTarget::create_multi_target(Device& device, RenderPass& render_pass, Math::Vec2u size, uint32_t frame_count) {
        auto attempt = try_create_multi_target(
            device, render_pass, size, frame_count);
        if(!attempt) {
            LOG_FATAL("Failed to create multi render target: {}",
                vk::to_string(attempt.result()));
        }
        return std::move(attempt).value();
    }

    GpuResourceResult<std::unique_ptr<RenderTarget>>
    RenderTarget::try_create_multi_target(
        Device& device,
        RenderPass& render_pass,
        const Math::Vec2u size,
        const uint32_t frame_count) {
        if(size.x == 0 || size.y == 0 || frame_count == 0) {
            LOG_FATAL("Multi render target requires a non-zero extent and frame count");
        }

        std::unique_ptr<MultiTarget> target(new MultiTarget(
            device,
            render_pass,
            size,
            frame_count));
        const auto initialization = target->try_initialize();
        if(!initialization) {
            return GpuResourceResult<std::unique_ptr<RenderTarget>>::failure(
                initialization.result());
        }
        return GpuResourceResult<std::unique_ptr<RenderTarget>>::success(
            std::move(target));
    }

    // RenderTarget
    void RenderTarget::set_clear_value(const ClearValue& clear_value) {
        const auto& attachments = m_render_pass.get_attachments();
        for(std::size_t index = 0; index < attachments.size(); ++index) {
            set_clear_value(clear_value, index);
        }
    }

    void RenderTarget::set_clear_value(
        const ClearValue& clear_value, const std::size_t index) {
        const auto& attachments = m_render_pass.get_attachments();
        if(index >= attachments.size()) return;

        const auto& description = attachments[index].description;
        if(description.load_op != AttachmentLoadOp::Clear) return;

        const bool is_depth_stencil = Graphics::is_depth_stencil_format(description.format);
        if((clear_value.is_color() && !is_depth_stencil) ||
           (clear_value.is_depth_stencil() && is_depth_stencil)) {
            m_clear_values[index] = clear_value;
        }
    }

    void RenderTarget::begin_render_target(CommandBuffer& command_buffer) {
        begin_render_target(command_buffer, (m_current_image_index + 1) % m_frame_count);
    }

    void RenderTarget::begin_render_target(
        const CommandBuffer& command_buffer, const uint32_t frame_index) {
        if(frame_index >= m_frame_count) {
            LOG_FATAL("Render target frame index {} exceeds frame count {}",
                frame_index, m_frame_count);
        }
        m_current_image_index = frame_index;
        command_buffer.begin_render_pass(m_render_pass, *get_framebuffer(m_current_image_index), m_clear_values);
    }

    void RenderTarget::end_render_target(CommandBuffer& command_buffer) {
        command_buffer.end_render_pass();
    }

    void RenderTarget::clear_render_resources(std::vector<RenderResource>& resources) {
        for(auto& [color_views, frame_buffer]: resources) {
            frame_buffer.reset();
            color_views.clear();
        }
        resources.clear();
    }

    // SwapchainTarget
    SwapchainTarget::SwapchainTarget(
        Device& device,
        RenderPass& render_pass,
        std::shared_ptr<SwapchainGeneration> swapchain_generation)
        : RenderTarget(
            device,
            render_pass,
            Math::Vec2u(
                swapchain_generation->get_config().extent.width,
                swapchain_generation->get_config().extent.height),
            swapchain_generation->get_images().size()),
          m_swapchain_generation(std::move(swapchain_generation)) {
        m_clear_values.resize(m_render_pass.get_attachments().size());
        set_clear_value(ClearValue(Math::Vec4(0.2f, 0.3f, 0.3f, 1.0f)));
        set_clear_value(ClearValue(1.0f, 0));

        if(m_extent.x == 0 || m_extent.y == 0) {
            return;
        }
        m_render_resources.clear();
        m_render_resources.resize(m_frame_count);

        const auto attachments = m_render_pass.get_attachments();
        if(attachments.empty()) {
            return;
        }

        for(uint32_t i = 0; i < m_frame_count; ++i) {
            std::vector<std::shared_ptr<ImageView>> all_views;
            std::vector<std::shared_ptr<ImageView>> color_views;

            for(const auto& [description, usage]: attachments) {
                ImageInfo image_info = {};
                image_info.format = description.format;
                image_info.extent = {m_extent.x, m_extent.y, 1};
                image_info.usage = usage;

                if(Graphics::is_depth_stencil_format(description.format)) {
                    auto depth_image = Image::create(
                        m_device, image_info, description.samples, "render target depth image");
                    all_views.push_back(ImageView::create(
                        m_device, depth_image, Flags<ImageAspect>(ImageAspect::Depth)));
                } else {
                    std::shared_ptr<Image> color_image;
                    if(description.final_layout == ImageLayout::PresentSrcKHR && description.samples == SampleCount::Count1) {
                        color_image = m_swapchain_generation->get_images()[i];
                    } else {
                        color_image = Image::create(
                            m_device, image_info, description.samples, "render target color image");
                    }
                    auto color_view = ImageView::create(
                        m_device, color_image, Flags<ImageAspect>(ImageAspect::Color));
                    color_views.emplace_back(color_view);
                    all_views.push_back(color_view);
                }
            }

            m_render_resources[i].frame_buffer = FrameBuffer::create(
                m_device, m_render_pass, all_views, m_extent.x, m_extent.y);
            m_render_resources[i].color_views = std::move(color_views);
        }
    }

    SwapchainTarget::~SwapchainTarget() {
        clear_render_resources(m_render_resources);
    }

    void SwapchainTarget::begin_render_target(CommandBuffer& command_buffer) {
        RenderTarget::begin_render_target(
            command_buffer, m_swapchain_generation->get_current_index());
    }

    MultiTarget::MultiTarget(
        Device& device,
        RenderPass& render_pass,
        const Math::Vec2u size,
        const uint32_t frame_count)
        : RenderTarget(device, render_pass, size, frame_count) {
        m_clear_values.resize(m_render_pass.get_attachments().size());
        set_clear_value(ClearValue(Math::Vec4(0.2f, 0.3f, 0.3f, 1.0f)));
        set_clear_value(ClearValue(1.0f, 0));
    }

    MultiTarget::~MultiTarget() {
        clear_render_resources(m_render_resources);
    }

    GpuResourceResult<void> MultiTarget::try_initialize() {
        std::vector<RenderResource> resources;
        resources.reserve(m_frame_count);
        for(uint32_t index = 0; index < m_frame_count; ++index) {
            auto resource_attempt = try_create_owned_render_resource(
                m_device, m_render_pass, m_extent);
            if(!resource_attempt) {
                return GpuResourceResult<void>::failure(
                    resource_attempt.result());
            }
            resources.push_back(std::move(resource_attempt).value());
        }

        clear_render_resources(m_render_resources);
        m_render_resources = std::move(resources);
        return GpuResourceResult<void>::success();
    }
}
