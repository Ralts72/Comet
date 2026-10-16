#include "render/passes/deferred_pass.h"

#include "graphics/device.h"
#include "graphics/attachment.h"
#include "graphics/frame_buffer.h"
#include "graphics/render_pass.h"
#include "graphics/pipeline/pipeline.h"
#include "graphics/pipeline/shader.h"
#include "graphics/resource/image_view.h"
#include "graphics/resource/sampler.h"
#include "render/render_target.h"
#include "render/frame_scheduler.h"
#include "render/material/material_renderer.h"
#include "render/resource/sampled_image_binding.h"
#include "display_vert.h"
#include "deferred_frag.h"

namespace Comet {
    DeferredPass::~DeferredPass() = default;

    Result<std::unique_ptr<DeferredPass>, GraphicsError> DeferredPass::create(
        Device& device, uint32_t frame_slots) {
        using Created = Result<std::unique_ptr<DeferredPass>, GraphicsError>;
        if(!frame_slots)
            return Created::failure({"Deferred rendering requires frame slots"});
        if(device.get_capability().physical_device.getProperties().limits.maxColorAttachments
            < COLOR_COUNT)
            return Created::failure({"Deferred rendering requires four color attachments"});
        auto result = std::unique_ptr<DeferredPass>(new DeferredPass(device, frame_slots));
        std::vector<Attachment> attachments;
        RenderSubPass subpass;
        for(uint32_t index = 0; index < COLOR_COUNT; ++index) {
            auto color = Attachment::get_color_attachment(Format::R16G16B16A16_SFLOAT);
            color.description.store_op = AttachmentStoreOp::Store;
            color.description.initial_layout = ImageLayout::ColorAttachmentOptimal;
            color.description.final_layout = ImageLayout::ColorAttachmentOptimal;
            color.usage |= ImageUsage::Sampled;
            attachments.push_back(color);
            subpass.color_attachments.emplace_back(index);
        }
        auto depth = Attachment::get_depth_attachment(Format::D32_SFLOAT);
        const auto supported = device.query_format_support(Format::D32_SFLOAT);
        if(!supported.depth_attachment || !supported.sampled)
            return Created::failure({"Deferred rendering requires sampled D32 depth"});
        depth.description.store_op = AttachmentStoreOp::Store;
        depth.description.initial_layout = ImageLayout::DepthStencilAttachmentOptimal;
        depth.usage |= ImageUsage::Sampled;
        attachments.push_back(depth);
        subpass.depth_stencil_attachments.emplace_back(COLOR_COUNT);
        auto pass = RenderPass::create(device, attachments, {subpass});
        if(!pass)
            return Created::failure(pass.error());
        result->m_pass = std::move(pass).value();
        result->m_pipelines = std::make_unique<PipelineManager>(device, *result->m_pass);
        return Created::success(std::move(result));
    }

    Result<void, GraphicsError> DeferredPass::create_lighting(
        PipelineManager& pipelines, const std::shared_ptr<DescriptorSetLayout>& view_layout) {
        DescriptorSetLayoutBindings bindings;
        for(uint32_t index = 0; index <= COLOR_COUNT; ++index)
            bindings.add_binding(index, DescriptorType::CombinedImageSampler,
                Flags<ShaderStage>(ShaderStage::Fragment));
        auto layout = DescriptorSetLayout::create(m_device, bindings);
        if(!layout)
            return Result<void, GraphicsError>::failure(layout.error());
        auto sampler =
            Sampler::create(m_device, {.mag_filter = Filter::Nearest,
                                          .min_filter = Filter::Nearest,
                                          .address_mode_u = SamplerAddressMode::ClampToEdge,
                                          .address_mode_v = SamplerAddressMode::ClampToEdge});
        if(!sampler)
            return Result<void, GraphicsError>::failure(sampler.error());
        auto vertex = Shader::create(m_device, "deferred fullscreen", DISPLAY_VERT);
        auto fragment = Shader::create(m_device, "deferred lighting", DEFERRED_FRAG);
        if(!vertex)
            return Result<void, GraphicsError>::failure(vertex.error());
        if(!fragment)
            return Result<void, GraphicsError>::failure(fragment.error());
        ShaderLayout shader_layout;
        shader_layout.descriptor_set_layouts = {view_layout, layout.value()};
        PipelineConfig config;
        config.set_dynamic_state({DynamicState::Viewport, DynamicState::Scissor});
        config.enable_depth_test();
        config.depth_stencil_state.depth_compare_op = CompareOp::Always;
        auto lighting = pipelines.create_pipeline(
            "deferred lighting", shader_layout, config, vertex.value(), fragment.value());
        if(!lighting)
            return Result<void, GraphicsError>::failure(lighting.error());
        m_layout = std::move(layout).value();
        m_sampler = std::move(sampler).value();
        m_lighting = std::move(lighting).value();
        return Result<void, GraphicsError>::success();
    }

    std::span<const std::shared_ptr<ImageView>> DeferredPass::images(
        const Resources& resources, uint32_t slot) {
        return resources.target->get_framebuffer(slot)->get_attachments();
    }

    Result<std::shared_ptr<DeferredPass::Resources>, GraphicsError> DeferredPass::create_resources(
        Math::Vec2u size) const {
        using Created = Result<std::shared_ptr<Resources>, GraphicsError>;
        auto target = RenderTarget::try_create_multi_target(m_device, *m_pass, size, m_frame_slots);
        if(!target)
            return Created::failure(target.error());
        auto resources = std::make_shared<Resources>();
        resources->target = std::move(target).value();
        resources->target->set_clear_value(ClearValue(Math::Vec4(0)));
        for(uint32_t slot = 0; slot < m_frame_slots; ++slot) {
            const auto views = images(*resources, slot);
            auto binding = SampledImageBinding::create(
                m_device, {views.begin(), views.end()}, m_layout, m_sampler);
            if(!binding)
                return Created::failure(binding.error());
            resources->bindings.push_back(std::move(binding).value());
        }
        return Created::success(std::move(resources));
    }

    void DeferredPass::record_geometry(FrameScheduler& frames,
        const std::shared_ptr<Resources>& resources, MaterialRenderer& materials,
        std::vector<QueueSemaphoreSubmit>& waits) {
        frames.retain_current_frame_resource(resources);
        auto& command = frames.get_current_command_buffer();
        resources->target->begin_render_target(command, frames.get_current_frame_slot_index());
        const auto size = resources->target->get_size();
        command.set_viewport(Graphics::get_viewport(float(size.x), float(size.y)));
        command.set_scissor(Graphics::get_scissor(float(size.x), float(size.y)));
        materials.record_gbuffer(frames, waits);
        resources->target->end_render_target(command);
    }

    void DeferredPass::record_lighting(FrameScheduler& frames,
        const std::shared_ptr<Resources>& resources, const DescriptorSet& view) {
        const auto slot = frames.get_current_frame_slot_index();
        const std::array sets{view, resources->bindings.at(slot)->descriptor};
        auto& command = frames.get_current_command_buffer();
        command.bind_pipeline(*m_lighting);
        command.bind_descriptor_sets(*m_lighting->get_layout(), sets);
        command.draw(3);
        frames.retain_current_frame_resource(resources);
        frames.retain_current_frame_resource(m_lighting);
    }
}
