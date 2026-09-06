#include "render/post_process_renderer.h"

#include "graphics/device.h"
#include "graphics/convert.h"
#include "graphics/render_pass.h"
#include "graphics/pipeline/pipeline.h"
#include "graphics/pipeline/shader.h"
#include "graphics/resource/image.h"
#include "graphics/resource/image_view.h"
#include "graphics/resource/sampler.h"
#include "render/frame_scheduler.h"
#include "render/render_target.h"
#include "fullscreen_vert.h"
#include "tone_map_frag.h"
#include "bloom_frag.h"

#include <cmath>
#include <stdexcept>

namespace Comet {
    struct PostProcessRenderer::Binding {
        std::array<std::shared_ptr<ImageView>, 2> images;
        std::shared_ptr<DescriptorPool> pool;
        DescriptorSet descriptor;
    };

    struct PostProcessRenderer::Parameters {
        float exposure;
        uint32_t encode_srgb;
        float bloom_strength;
        float bloom_threshold;
        uint32_t mode;
    };

    bool PostProcessRenderer::Settings::is_valid() const {
        return std::isfinite(exposure) && exposure >= 0 && exposure <= 100
               && std::isfinite(bloom_strength) && bloom_strength >= 0
               && bloom_strength <= 10 && std::isfinite(bloom_threshold)
               && bloom_threshold >= 0 && bloom_threshold <= 65504;
    }

    PostProcessRenderer::PostProcessRenderer(Device& device, const Format output_format,
        const bool offscreen, const uint32_t frame_slots)
        : m_device(device), m_bindings(frame_slots), m_encode_srgb(false) {
        switch(output_format) {
            case Format::R8G8B8A8_SRGB:
            case Format::B8G8R8A8_SRGB:
                break;
            case Format::R8G8B8A8_UNORM:
            case Format::B8G8R8A8_UNORM:
                m_encode_srgb = true;
                break;
            default:
                throw std::invalid_argument("Post process requires an SDR RGBA output");
        }
        if(frame_slots == 0)
            throw std::invalid_argument("Post process requires frame slots");
        auto color = Attachment::get_color_attachment(output_format);
        color.description.store_op = AttachmentStoreOp::Store;
        if(offscreen) {
            color.description.final_layout = ImageLayout::ShaderReadOnlyOptimal;
            color.usage |= ImageUsage::Sampled;
            color.usage |= ImageUsage::CopySrc;
        }
        m_render_pass =
            std::make_shared<RenderPass>(device, std::vector<Attachment>{color},
                std::vector<RenderSubPass>{
                    {{}, {SubpassColorAttachment(0)}, {}, SampleCount::Count1}},
                output_format);

        DescriptorSetLayoutBindings bindings;
        bindings.add_binding(0, DescriptorType::CombinedImageSampler,
            Flags<ShaderStage>(ShaderStage::Fragment));
        bindings.add_binding(1, DescriptorType::CombinedImageSampler,
            Flags<ShaderStage>(ShaderStage::Fragment));
        m_layout = std::make_shared<DescriptorSetLayout>(device, bindings);
        m_sampler = Sampler::create_nearest_clamp(device);
        ShaderLayout layout;
        layout.descriptor_set_layouts = {m_layout};
        static_assert(sizeof(Parameters) == 20);
        layout.push_constants.push_back(std::make_shared<PushConstantRange>(
            ShaderStage::Fragment, 0, sizeof(Parameters)));
        PipelineConfig config;
        config.set_dynamic_state({DynamicState::Viewport, DynamicState::Scissor});
        PipelineManager pipelines(device, *m_render_pass);
        m_pipeline = pipelines.create_pipeline("tone_map", layout, config,
            std::make_shared<Shader>(device, "fullscreen", FULLSCREEN_VERT),
            std::make_shared<Shader>(device, "tone_map", TONE_MAP_FRAG));
        auto hdr = Attachment::get_color_attachment(Format::R16G16B16A16_SFLOAT);
        hdr.description.initial_layout = ImageLayout::ColorAttachmentOptimal;
        hdr.description.final_layout = ImageLayout::ColorAttachmentOptimal;
        hdr.description.store_op = AttachmentStoreOp::Store;
        hdr.usage |= ImageUsage::Sampled;
        m_bloom_pass = std::make_shared<RenderPass>(device, std::vector<Attachment>{hdr},
            std::vector<RenderSubPass>{
                {{}, {SubpassColorAttachment(0)}, {}, SampleCount::Count1}},
            Format::R16G16B16A16_SFLOAT);
        PipelineManager bloom_pipelines(device, *m_bloom_pass);
        m_bloom_pipeline = bloom_pipelines.create_pipeline("bloom", layout, config,
            std::make_shared<Shader>(device, "fullscreen", FULLSCREEN_VERT),
            std::make_shared<Shader>(device, "bloom", BLOOM_FRAG));
    }

    void PostProcessRenderer::append_passes(
        RenderGraph& graph, const RenderGraph::ResourceId hdr, const bool bloom) {
        const auto sampled = [](const auto id) {
            return RenderGraph::Use{id, ResourceUsage::SampledRead,
                Flags<PipelineStage>(PipelineStage::FragmentShader)};
        };
        RenderGraph::Pass compose{"tone map", {sampled(hdr)}};
        if(bloom) {
            const auto initial = *resolve_image_state(ResourceUsage::Undefined,
                {.aspects = Flags<ImageAspect>(ImageAspect::Color)});
            const auto first = graph.import_image("bloom ping", initial);
            const auto second = graph.import_image("bloom pong", initial);
            graph.add_pass({"bloom extract",
                {sampled(hdr), {first, ResourceUsage::ColorAttachmentWrite, {}}}});
            graph.add_pass({"bloom horizontal",
                {sampled(first), {second, ResourceUsage::ColorAttachmentWrite, {}}}});
            graph.add_pass({"bloom vertical",
                {sampled(second), {first, ResourceUsage::ColorAttachmentWrite, {}}}});
            compose.uses.push_back(sampled(first));
        }
        graph.add_pass(std::move(compose));
    }

    void PostProcessRenderer::append_bindings(std::vector<RenderGraph::Binding>& bindings,
        const uint32_t slot, const bool bloom) const {
        if(bloom)
            for(const auto& target : m_bloom_targets) {
                if(!target)
                    throw std::logic_error("Bloom targets are not prepared");
                bindings.emplace_back(target->get_color_view(slot)->get_image());
            }
    }

    GpuResourceResult<void> PostProcessRenderer::try_resize_bloom(Math::Vec2u size) {
        if(size.x == 0 || size.y == 0)
            return GpuResourceResult<void>::failure(
                vk::Result::eErrorInitializationFailed);
        size = {size.x / 2 + size.x % 2, size.y / 2 + size.y % 2};
        if(m_bloom_targets[0] && m_bloom_targets[0]->get_size() == size)
            return GpuResourceResult<void>::success();
        std::array<std::shared_ptr<RenderTarget>, 2> candidates;
        for(auto& target : candidates) {
            auto result = RenderTarget::try_create_multi_target(
                m_device, *m_bloom_pass, size, static_cast<uint32_t>(m_bindings.size()));
            if(!result)
                return GpuResourceResult<void>::failure(result.result());
            target = std::move(result).value();
        }
        m_bloom_targets = std::move(candidates);
        for(auto& slot : m_bindings)
            slot = {};
        return GpuResourceResult<void>::success();
    }

    void PostProcessRenderer::render_pass(const size_t pass, FrameScheduler& frames,
        const std::shared_ptr<RenderTarget>& output, const uint32_t output_index,
        const std::shared_ptr<ImageView>& hdr_color, const Settings& settings) {
        const bool bloom = settings.uses_bloom();
        if(!frames.is_recording_frame() || !output || !hdr_color || !settings.is_valid()
            || pass > (bloom ? 3u : 0u)
            || (bloom && (!m_bloom_targets[0] || !m_bloom_targets[1])))
            throw std::invalid_argument("Invalid post process pass, input or settings");
        const auto extent = hdr_color->get_image()->get_info().extent;
        const Math::Vec2u expected_size{
            extent.x / 2 + extent.x % 2, extent.y / 2 + extent.y % 2};
        if(bloom && m_bloom_targets[0]->get_size() != expected_size)
            throw std::invalid_argument("Bloom targets do not match the HDR input size");
        const Parameters parameters{settings.exposure, m_encode_srgb ? 1u : 0u,
            settings.bloom_strength, settings.bloom_threshold,
            static_cast<uint32_t>(pass)};
        const auto slot = frames.get_current_frame_slot_index();
        if(bloom && pass < 3) {
            const auto input =
                pass == 0 ? hdr_color : m_bloom_targets[pass - 1]->get_color_view(slot);
            draw(frames, pass, m_bloom_pipeline, m_bloom_pass, m_bloom_targets[pass % 2],
                slot, input, input, parameters);
        } else {
            const auto glow =
                bloom ? m_bloom_targets[0]->get_color_view(slot) : hdr_color;
            draw(frames, 3, m_pipeline, m_render_pass, output, output_index, hdr_color,
                glow, parameters);
        }
    }

    void PostProcessRenderer::render(FrameScheduler& frames,
        const std::shared_ptr<RenderTarget>& output, const uint32_t output_index,
        const std::shared_ptr<ImageView>& hdr_color, const float exposure) {
        render_pass(0, frames, output, output_index, hdr_color, {.exposure = exposure});
    }

    void PostProcessRenderer::draw(FrameScheduler& frames, const size_t binding_index,
        const std::shared_ptr<Pipeline>& pipeline,
        const std::shared_ptr<RenderPass>& render_pass,
        const std::shared_ptr<RenderTarget>& output, const uint32_t output_index,
        const std::shared_ptr<ImageView>& first, const std::shared_ptr<ImageView>& second,
        const Parameters& parameters) {
        auto& binding =
            m_bindings.at(frames.get_current_frame_slot_index()).at(binding_index);
        const std::array images{first, second};
        if(!binding || binding->images != images) {
            DescriptorPoolSizes sizes;
            sizes.add_pool_size(DescriptorType::CombinedImageSampler, 2);
            auto pool = std::make_shared<DescriptorPool>(m_device, 1, sizes);
            const auto descriptor = pool->allocate_descriptor_set(*m_layout, 1).front();
            auto candidate = std::make_shared<Binding>(Binding{images, pool, descriptor});
            for(uint32_t index = 0; index < images.size(); ++index) {
                const vk::DescriptorImageInfo image(m_sampler->get(),
                    images[index]->get(), vk::ImageLayout::eShaderReadOnlyOptimal);
                vk::WriteDescriptorSet write;
                write.dstSet = descriptor.get();
                write.dstBinding = index;
                write.descriptorType = vk::DescriptorType::eCombinedImageSampler;
                write.descriptorCount = 1;
                write.pImageInfo = &image;
                m_device.get().updateDescriptorSets(write, {});
            }
            binding = std::move(candidate);
        }
        frames.retain_current_frame_resource(binding);
        frames.retain_current_frame_resource(pipeline);
        frames.retain_current_frame_resource(m_layout);
        frames.retain_current_frame_resource(m_sampler);
        frames.retain_current_frame_resource(render_pass);
        frames.retain_current_frame_resource(output);
        auto& command = frames.get_current_command_buffer();
        output->begin_render_target(command, output_index);
        const auto size = output->get_size();
        // 正高度 viewport 保持输入纹理与输出 framebuffer 的像素方向一致。
        command.set_viewport(vk::Viewport(0, 0, float(size.x), float(size.y), 0, 1));
        command.set_scissor(Graphics::get_scissor(float(size.x), float(size.y)));
        command.bind_pipeline(*pipeline);
        command.get().bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
            pipeline->get_layout()->get(), 0, binding->descriptor.get(), {});
        command.push_constants(*pipeline->get_layout(),
            Flags<ShaderStage>(ShaderStage::Fragment), 0, &parameters,
            sizeof(parameters));
        command.draw(3);
        output->end_render_target(command);
    }
}
