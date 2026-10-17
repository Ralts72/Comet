#include "render/passes/transparency_pass.h"
#include "render/frame_scheduler.h"
#include "graphics/pipeline/pipeline.h"
#include "render/resource/sampled_image_binding.h"
#include "graphics/pipeline/descriptor_set.h"
#include "graphics/render_pass.h"
#include "graphics/frame_buffer.h"
#include "graphics/attachment.h"
#include "graphics/pipeline/shader.h"
#include "graphics/resource/image_view.h"
#include "graphics/resource/sampler.h"
#include "graphics/command/command_buffer.h"
#include "graphics/convert.h"
#include "render/render_target.h"
#include "display_vert.h"
#include "transparency_composite_frag.h"

namespace Comet {
    TransparencyPass::TransparencyPass(Device& device, uint32_t slots)
        : m_device(device), m_slots(slots) {}
    TransparencyPass::~TransparencyPass() = default;
    PipelineManager& TransparencyPass::pipelines() const {
        return *m_pipelines;
    }
    Result<std::unique_ptr<TransparencyPass>, GraphicsError> TransparencyPass::create(
        Device& device, uint32_t slots) {
        using Created = Result<std::unique_ptr<TransparencyPass>, GraphicsError>;
        auto next = std::unique_ptr<TransparencyPass>(new TransparencyPass(device, slots));
        auto color =
            Attachment::get_color_attachment(Format::R16G16B16A16_SFLOAT, SampleCount::Count1);
        color.description.initial_layout = ImageLayout::ColorAttachmentOptimal;
        color.description.final_layout = ImageLayout::ColorAttachmentOptimal;
        color.description.store_op = AttachmentStoreOp::Store;
        color.usage |= ImageUsage::Sampled;
        auto accumulation = RenderPass::create(device, {color, color},
            {{.color_attachments = {SubpassColorAttachment(0), SubpassColorAttachment(1)}}});
        auto composite = RenderPass::create(
            device, {color}, {{.color_attachments = {SubpassColorAttachment(0)}}});
        if(!accumulation || !composite)
            return Created::failure(!accumulation ? accumulation.error() : composite.error());
        next->m_accumulation_pass = std::move(accumulation).value();
        next->m_composite_pass = std::move(composite).value();
        next->m_pipelines = std::make_unique<PipelineManager>(device, *next->m_accumulation_pass);
        DescriptorSetLayoutBindings depth_bindings;
        depth_bindings.add_binding(
            0, DescriptorType::SampledImage, Flags<ShaderStage>(ShaderStage::Fragment));
        auto depth_layout = DescriptorSetLayout::create(device, depth_bindings);
        DescriptorSetLayoutBindings image_bindings;
        for(uint32_t i = 0; i < 3; ++i)
            image_bindings.add_binding(
                i, DescriptorType::CombinedImageSampler, Flags<ShaderStage>(ShaderStage::Fragment));
        auto image_layout = DescriptorSetLayout::create(device, image_bindings);
        auto sampler =
            Sampler::create(device, {.mag_filter = Filter::Nearest,
                                        .min_filter = Filter::Nearest,
                                        .address_mode_u = SamplerAddressMode::ClampToEdge,
                                        .address_mode_v = SamplerAddressMode::ClampToEdge});
        if(!depth_layout || !image_layout || !sampler)
            return Created::failure(!depth_layout   ? depth_layout.error()
                                    : !image_layout ? image_layout.error()
                                                    : sampler.error());
        next->m_depth_layout = std::move(depth_layout).value();
        next->m_composite_layout = std::move(image_layout).value();
        next->m_sampler = std::move(sampler).value();
        auto vertex = Shader::create(device, "transparency fullscreen", DISPLAY_VERT);
        auto fragment =
            Shader::create(device, "transparency composite", TRANSPARENCY_COMPOSITE_FRAG);
        if(!vertex || !fragment)
            return Created::failure(!vertex ? vertex.error() : fragment.error());
        ShaderLayout layout;
        layout.descriptor_set_layouts = {next->m_composite_layout};
        PipelineConfig config;
        config.set_dynamic_state({DynamicState::Viewport, DynamicState::Scissor});
        PipelineManager pipelines(device, *next->m_composite_pass);
        auto pipeline = pipelines.create_pipeline(
            "transparency composite", layout, config, vertex.value(), fragment.value());
        if(!pipeline)
            return Created::failure(pipeline.error());
        next->m_composite = std::move(pipeline).value();
        return Created::success(std::move(next));
    }
    Result<std::shared_ptr<TransparencyPass::Resources>, GraphicsError> TransparencyPass::
        create_resources(Math::Vec2u size) const {
        using Created = Result<std::shared_ptr<Resources>, GraphicsError>;
        auto first =
            RenderTarget::try_create_multi_target(m_device, *m_accumulation_pass, size, m_slots);
        auto second =
            RenderTarget::try_create_multi_target(m_device, *m_composite_pass, size, m_slots);
        if(!first || !second)
            return Created::failure(!first ? first.error() : second.error());
        auto next = std::make_shared<Resources>();
        next->accumulation = std::move(first).value();
        next->output = std::move(second).value();
        next->accumulation->set_clear_value(ClearValue(Math::Vec4(0)));
        next->depth.resize(m_slots);
        next->composite.resize(m_slots);
        return Created::success(std::move(next));
    }
    std::array<std::shared_ptr<ImageView>, 3> TransparencyPass::images(
        const Resources& resources, uint32_t slot) {
        const auto attachments = resources.accumulation->get_framebuffer(slot)->get_attachments();
        return {attachments[0], attachments[1], resources.output->get_color_view(slot)};
    }
    Result<DescriptorSet, GraphicsError> TransparencyPass::begin(FrameScheduler& frames,
        const std::shared_ptr<Resources>& resources, const std::shared_ptr<ImageView>& depth) {
        const auto slot = frames.get_current_frame_slot_index();
        auto& binding = resources->depth.at(slot);
        if(!binding || binding->images.front() != depth) {
            auto candidate =
                SampledImageBinding::create_separate(m_device, {depth}, m_depth_layout, {});
            if(!candidate)
                return Result<DescriptorSet, GraphicsError>::failure(candidate.error());
            binding = std::move(candidate).value();
        }
        frames.retain_current_frame_resource(binding);
        frames.retain_current_frame_resource(resources);
        frames.retain_current_frame_resource(m_accumulation_pass);
        auto& command = frames.get_current_command_buffer();
        resources->accumulation->begin_render_target(command, slot);
        const auto size = resources->accumulation->get_size();
        command.set_viewport(Graphics::get_viewport(float(size.x), float(size.y)));
        command.set_scissor(Graphics::get_scissor(float(size.x), float(size.y)));
        return Result<DescriptorSet, GraphicsError>::success(binding->descriptor);
    }
    Result<void, GraphicsError> TransparencyPass::composite(FrameScheduler& frames,
        const std::shared_ptr<Resources>& resources, const std::shared_ptr<ImageView>& scene) {
        const auto slot = frames.get_current_frame_slot_index();
        const auto targets = images(*resources, slot);
        const std::vector images{scene, targets[0], targets[1]};
        auto& binding = resources->composite.at(slot);
        if(!binding || binding->images != images) {
            auto candidate =
                SampledImageBinding::create(m_device, images, m_composite_layout, m_sampler);
            if(!candidate)
                return Result<void, GraphicsError>::failure(candidate.error());
            binding = std::move(candidate).value();
        }
        frames.retain_current_frame_resource(binding);
        frames.retain_current_frame_resource(resources);
        frames.retain_current_frame_resource(m_composite);
        frames.retain_current_frame_resource(m_composite_pass);
        auto& command = frames.get_current_command_buffer();
        resources->output->begin_render_target(command, slot);
        const auto size = resources->output->get_size();
        command.set_viewport(Graphics::get_viewport(float(size.x), float(size.y)));
        command.set_scissor(Graphics::get_scissor(float(size.x), float(size.y)));
        command.bind_pipeline(*m_composite);
        command.bind_descriptor_sets(
            *m_composite->get_layout(), std::span(&binding->descriptor, 1));
        command.draw(3);
        resources->output->end_render_target(command);
        return Result<void, GraphicsError>::success();
    }
}
