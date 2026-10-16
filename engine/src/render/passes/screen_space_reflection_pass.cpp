#include "render/passes/screen_space_reflection_pass.h"

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
#include "display_vert.h"
#include "screen_space_reflection_frag.h"

#include <array>
#include <algorithm>

namespace Comet {
    Result<std::unique_ptr<ScreenSpaceReflectionPass>, GraphicsError> ScreenSpaceReflectionPass::
        create(Device& device, uint32_t slots,
            const std::shared_ptr<DescriptorSetLayout>& view_layout) {
        using Created = Result<std::unique_ptr<ScreenSpaceReflectionPass>, GraphicsError>;
        if(slots == 0)
            return Created::failure({"SSR requires frame slots"});
        uint32_t sampler_count = 2, image_count = 7;
        for(const auto& binding : view_layout->get_bindings()) {
            if(!(binding.stageFlags & vk::ShaderStageFlagBits::eFragment))
                continue;
            if(binding.descriptorType == vk::DescriptorType::eCombinedImageSampler
                || binding.descriptorType == vk::DescriptorType::eSampler)
                sampler_count += binding.descriptorCount;
            if(binding.descriptorType == vk::DescriptorType::eCombinedImageSampler
                || binding.descriptorType == vk::DescriptorType::eSampledImage)
                image_count += binding.descriptorCount;
        }
        const auto limits = device.get_capability().physical_device.getProperties().limits;
        if(sampler_count > limits.maxPerStageDescriptorSamplers
            || image_count > limits.maxPerStageDescriptorSampledImages)
            return Created::failure({"SSR exceeds the device's fragment image/sampler budget"});
        auto next = std::unique_ptr<ScreenSpaceReflectionPass>(
            new ScreenSpaceReflectionPass(device, slots));
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
        for(uint32_t index = 0; index < 7; ++index)
            bindings.add_binding(
                index, DescriptorType::SampledImage, Flags<ShaderStage>(ShaderStage::Fragment));
        for(uint32_t index : {7u, 8u})
            bindings.add_binding(
                index, DescriptorType::Sampler, Flags<ShaderStage>(ShaderStage::Fragment));
        auto layout = DescriptorSetLayout::create(device, bindings);
        if(!layout)
            return Created::failure(layout.error());
        next->m_layout = std::move(layout).value();
        SamplerDesc sampler_info{.mag_filter = Filter::Nearest,
            .min_filter = Filter::Nearest,
            .address_mode_u = SamplerAddressMode::ClampToEdge,
            .address_mode_v = SamplerAddressMode::ClampToEdge};
        auto nearest = Sampler::create(device, sampler_info);
        sampler_info.mag_filter = sampler_info.min_filter = Filter::Linear;
        auto linear = Sampler::create(device, sampler_info);
        if(!nearest)
            return Created::failure(nearest.error());
        if(!linear)
            return Created::failure(linear.error());
        next->m_nearest = std::move(nearest).value();
        next->m_linear = std::move(linear).value();
        auto vertex = Shader::create(device, "SSR fullscreen", DISPLAY_VERT);
        auto fragment = Shader::create(device, "SSR", SCREEN_SPACE_REFLECTION_FRAG);
        if(!vertex)
            return Created::failure(vertex.error());
        if(!fragment)
            return Created::failure(fragment.error());
        ShaderLayout shader_layout;
        shader_layout.descriptor_set_layouts = {view_layout, next->m_layout};
        shader_layout.push_constants.push_back(
            std::make_shared<PushConstantRange>(ShaderStage::Fragment, 0, 16));
        PipelineConfig config;
        config.set_dynamic_state({DynamicState::Viewport, DynamicState::Scissor});
        PipelineManager pipelines(device, *next->m_pass);
        auto pipeline = pipelines.create_pipeline(
            "screen space reflections", shader_layout, config, vertex.value(), fragment.value());
        if(!pipeline)
            return Created::failure(pipeline.error());
        next->m_pipeline = std::move(pipeline).value();
        return Created::success(std::move(next));
    }

    Result<std::shared_ptr<ScreenSpaceReflectionPass::Resources>, GraphicsError>
    ScreenSpaceReflectionPass::create_resources(const RenderGraph::Resources& resources,
        RenderGraph::ResourceId output, Math::Vec2u size) const {
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

    std::shared_ptr<ImageView> ScreenSpaceReflectionPass::output(
        const Resources& resources, uint32_t slot) {
        return resources.images.at(slot);
    }

    Result<void, GraphicsError> ScreenSpaceReflectionPass::render(FrameScheduler& frames,
        const std::shared_ptr<Resources>& resources, const DescriptorSet& view,
        std::span<const std::shared_ptr<ImageView>> gbuffer, const std::shared_ptr<ImageView>& hdr,
        const std::shared_ptr<ImageView>& depth, const ScreenSpaceReflectionSettings& settings) {
        const auto slot = frames.get_current_frame_slot_index();
        const std::array images{
            gbuffer[0], gbuffer[1], gbuffer[2], gbuffer[3], gbuffer[4], hdr, depth};
        auto& binding = resources->bindings.at(slot);
        if(!binding || !std::ranges::equal(binding->images, images)) {
            auto created = SampledImageBinding::create_separate(
                m_device, {images.begin(), images.end()}, m_layout, {m_nearest, m_linear});
            if(!created)
                return Result<void, GraphicsError>::failure(created.error());
            binding = std::move(created).value();
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
        const std::array descriptors{view, binding->descriptor};
        command.bind_descriptor_sets(*m_pipeline->get_layout(), descriptors);
        const Math::Vec4 parameters{
            settings.strength, settings.max_distance, settings.thickness, settings.max_roughness};
        command.push_constants(*m_pipeline->get_layout(), Flags<ShaderStage>(ShaderStage::Fragment),
            0, &parameters, sizeof(parameters));
        command.draw(3);
        command.end_render_pass();
        return Result<void, GraphicsError>::success();
    }
}
