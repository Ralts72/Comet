#include "render/passes/outline_pass.h"
#include "graphics/device.h"
#include "graphics/attachment.h"
#include "graphics/frame_buffer.h"
#include "graphics/render_pass.h"
#include "graphics/pipeline/pipeline.h"
#include "graphics/pipeline/shader.h"
#include "graphics/resource/image_view.h"
#include "graphics/resource/image.h"
#include "graphics/resource/sampler.h"
#include "render/frame_scheduler.h"
#include "render/resource/sampled_image_binding.h"
#include "render/scene/render_geometry.h"
#include "display_vert.h"
#include "outline_composite_frag.h"
#include <algorithm>

namespace Comet {
    Result<std::unique_ptr<OutlinePass>, GraphicsError> OutlinePass::create(
        Device& device, uint32_t slots, Math::Vec2u size) {
        using Created = Result<std::unique_ptr<OutlinePass>, GraphicsError>;
        auto next = std::unique_ptr<OutlinePass>(new OutlinePass(device, slots));
        auto mask = DepthPass::create(device, slots, size, 1, DepthPass::Output::Outline);
        if(!mask)
            return Created::failure(mask.error());
        next->m_mask = std::move(mask).value();
        auto color = Attachment::get_color_attachment(Format::R16G16B16A16_SFLOAT);
        color.description.initial_layout = color.description.final_layout =
            ImageLayout::ColorAttachmentOptimal;
        color.description.store_op = AttachmentStoreOp::Store;
        color.usage |= ImageUsage::Sampled;
        auto pass = RenderPass::create(
            device, {color}, {{.color_attachments = {SubpassColorAttachment(0)}}});
        if(!pass)
            return Created::failure(pass.error());
        next->m_pass = std::move(pass).value();
        DescriptorSetLayoutBindings bindings;
        for(uint32_t index = 0; index < 5; ++index)
            bindings.add_binding(
                index, DescriptorType::SampledImage, Flags<ShaderStage>(ShaderStage::Fragment));
        bindings.add_binding(5, DescriptorType::Sampler, Flags<ShaderStage>(ShaderStage::Fragment));
        auto layout = DescriptorSetLayout::create(device, bindings);
        if(!layout)
            return Created::failure(layout.error());
        next->m_layout = std::move(layout).value();
        SamplerDesc sampler_info{.mag_filter = Filter::Nearest,
            .min_filter = Filter::Nearest,
            .address_mode_u = SamplerAddressMode::ClampToEdge,
            .address_mode_v = SamplerAddressMode::ClampToEdge};
        auto sampler = Sampler::create(device, sampler_info);
        if(!sampler)
            return Created::failure(sampler.error());
        next->m_sampler = std::move(sampler).value();
        auto vertex = Shader::create(device, "outline fullscreen", DISPLAY_VERT);
        auto fragment = Shader::create(device, "outline composite", OUTLINE_COMPOSITE_FRAG);
        if(!vertex)
            return Created::failure(vertex.error());
        if(!fragment)
            return Created::failure(fragment.error());
        ShaderLayout shader_layout;
        shader_layout.descriptor_set_layouts = {next->m_layout};
        shader_layout.push_constants.push_back(
            std::make_shared<PushConstantRange>(ShaderStage::Fragment, 0, 16));
        PipelineConfig config;
        config.set_dynamic_state({DynamicState::Viewport, DynamicState::Scissor});
        PipelineManager pipelines(device, *next->m_pass);
        auto pipeline = pipelines.create_pipeline(
            "outline composite", shader_layout, config, vertex.value(), fragment.value());
        if(!pipeline)
            return Created::failure(pipeline.error());
        next->m_pipeline = std::move(pipeline).value();
        return Created::success(std::move(next));
    }
    Result<std::shared_ptr<OutlinePass::Resources>, GraphicsError> OutlinePass::create_resources(
        const RenderGraph::Resources& resources, RenderGraph::ResourceId output,
        Math::Vec2u size) const {
        using Created = Result<std::shared_ptr<Resources>, GraphicsError>;
        auto next = std::make_shared<Resources>();
        next->size = size;
        for(uint32_t slot = 0; slot < m_slots; ++slot) {
            const auto bindings = resources.get_bindings(slot);
            const auto& image = std::get<std::shared_ptr<Image>>(bindings[output.index]);
            auto view =
                ImageView::try_create(m_device, image, Flags<ImageAspect>(ImageAspect::Color));
            if(!view)
                return Created::failure(view.error());
            auto framebuffer =
                FrameBuffer::try_create(m_device, *m_pass, {view.value()}, size.x, size.y);
            if(!framebuffer)
                return Created::failure(framebuffer.error());
            next->images.push_back(std::move(view).value());
            next->framebuffers.push_back(std::move(framebuffer).value());
        }
        next->bindings.resize(m_slots);
        return Created::success(std::move(next));
    }

    std::array<std::shared_ptr<ImageView>, 3> OutlinePass::mask_images(uint32_t slot) const {
        return {m_mask->get_depth_view(slot), m_mask->get_color_view(slot, 0),
            m_mask->get_color_view(slot, 1)};
    }
    Result<std::vector<QueueSemaphoreSubmit>, GraphicsError> OutlinePass::mask(
        FrameScheduler& frames, const ViewProjectMatrix& camera, const RenderGeometry& geometry) {
        m_width = 0;
        for(const auto& item : geometry.get_items())
            if(item.source->outline.active())
                m_width = std::max(m_width, item.source->outline.width);
        DepthPass::View view;
        view.view_projection = camera.projection * camera.view;
        return m_mask->render(
            frames, std::span(&view, 1), geometry, DepthPass::LodSelection::AlreadySelected);
    }
    Result<void, GraphicsError> OutlinePass::composite(FrameScheduler& frames,
        const std::shared_ptr<Resources>& resources, const std::shared_ptr<ImageView>& scene,
        const std::shared_ptr<ImageView>& depth, Math::Vec2u output_size) {
        const auto slot = frames.get_current_frame_slot_index();
        const auto mask = mask_images(slot);
        const std::array images{scene, mask[1], mask[2], mask[0], depth};
        auto& binding = resources->bindings.at(slot);
        if(!binding || !std::ranges::equal(binding->images, images)) {
            auto next = SampledImageBinding::create_separate(
                m_device, {images.begin(), images.end()}, m_layout, {m_sampler});
            if(!next)
                return Result<void, GraphicsError>::failure(next.error());
            binding = std::move(next).value();
        }
        frames.retain_current_frame_resource(binding);
        frames.retain_current_frame_resource(resources);
        frames.retain_current_frame_resource(m_pass);
        frames.retain_current_frame_resource(m_pipeline);
        auto& command = frames.get_current_command_buffer();
        const auto size = resources->size;
        command.begin_render_pass(
            *m_pass, *resources->framebuffers.at(slot), {ClearValue(Math::Vec4(0))});
        command.set_viewport(vk::Viewport(0, 0, float(size.x), float(size.y), 0, 1));
        command.set_scissor(Graphics::get_scissor(float(size.x), float(size.y)));
        command.bind_pipeline(*m_pipeline);
        const std::array sets{binding->descriptor};
        command.bind_descriptor_sets(*m_pipeline->get_layout(), sets);
        const Math::Vec4 scale{
            float(size.x) / output_size.x, float(size.y) / output_size.y, m_width, 0};
        command.push_constants(*m_pipeline->get_layout(), Flags<ShaderStage>(ShaderStage::Fragment),
            0, &scale, sizeof(scale));
        command.draw(3);
        command.end_render_pass();
        return Result<void, GraphicsError>::success();
    }
}
