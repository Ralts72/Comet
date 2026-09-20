#include "render/passes/bloom_pass.h"

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
#include "render/resource/sampled_image_binding.h"
#include "bloom_vert.h"
#include "bloom_frag.h"

#include <algorithm>
#include <cmath>
#include <cassert>

namespace Comet {
    BloomPass::BloomPass(Device& device, const uint32_t frame_slots)
        : m_device(device), m_frame_slots(frame_slots), m_bindings(frame_slots) {}

    Result<std::unique_ptr<BloomPass>, GraphicsError> BloomPass::create(
        Device& device, const uint32_t frame_slots) {
        using Creation = Result<std::unique_ptr<BloomPass>, GraphicsError>;
        if(frame_slots == 0)
            return Creation::failure({"Bloom requires frame slots"});
        auto next = std::unique_ptr<BloomPass>(new BloomPass(device, frame_slots));
        constexpr auto format = Format::R16G16B16A16_SFLOAT;
        if(auto supported = validate_color_target(
               device.get_capability().physical_device, format, SampleCount::Count1);
            !supported)
            return Creation::failure(supported.error());
        auto color = Attachment::get_color_attachment(format);
        color.description.initial_layout = color.description.final_layout =
            ImageLayout::ColorAttachmentOptimal;
        color.description.store_op = AttachmentStoreOp::Store;
        color.usage |= ImageUsage::Sampled;
        auto pass = RenderPass::create(
            device, {color}, {{.color_attachments = {SubpassColorAttachment(0)}}}, format);
        if(!pass)
            return Creation::failure(pass.error());
        next->m_render_pass = std::move(pass).value();
        DescriptorSetLayoutBindings bindings;
        bindings.add_binding(
            0, DescriptorType::CombinedImageSampler, Flags<ShaderStage>(ShaderStage::Fragment));
        auto layout = DescriptorSetLayout::create(device, bindings);
        if(!layout)
            return Creation::failure(layout.error());
        next->m_layout = std::move(layout).value();
        auto sampler =
            Sampler::create(device, {.mag_filter = Filter::Nearest,
                                        .min_filter = Filter::Nearest,
                                        .address_mode_u = SamplerAddressMode::ClampToEdge,
                                        .address_mode_v = SamplerAddressMode::ClampToEdge,
                                        .address_mode_w = SamplerAddressMode::ClampToEdge});
        if(!sampler)
            return Creation::failure(sampler.error());
        next->m_sampler = std::move(sampler).value();
        auto vertex = Shader::create(device, "bloom", BLOOM_VERT);
        if(!vertex)
            return Creation::failure(vertex.error());
        auto fragment = Shader::create(device, "bloom", BLOOM_FRAG);
        if(!fragment)
            return Creation::failure(fragment.error());
        ShaderLayout shader_layout;
        shader_layout.descriptor_set_layouts = {next->m_layout};
        shader_layout.push_constants.push_back(
            std::make_shared<PushConstantRange>(ShaderStage::Fragment, 0, 8));
        PipelineConfig config;
        config.set_dynamic_state({DynamicState::Viewport, DynamicState::Scissor});
        PipelineManager pipelines(device, *next->m_render_pass);
        auto pipeline = pipelines.create_pipeline(
            "bloom", shader_layout, config, vertex.value(), fragment.value());
        if(!pipeline)
            return Creation::failure(pipeline.error());
        next->m_pipeline = std::move(pipeline).value();
        return Creation::success(std::move(next));
    }

    BloomPass::Passes BloomPass::append_passes(
        RenderGraph& graph, const RenderGraph::ResourceId hdr) {
        const auto ping = graph.import_image(
            "bloom ping", {.subresources = {.aspects = Flags<ImageAspect>(ImageAspect::Color)}});
        const auto pong = graph.import_image(
            "bloom pong", {.subresources = {.aspects = Flags<ImageAspect>(ImageAspect::Color)}});
        const Flags<PipelineStage> fragment(PipelineStage::FragmentShader);
        const auto extract =
            graph.add_pass({"bloom extract", {{hdr, ResourceUsage::SampledRead, fragment},
                                                 {ping, ResourceUsage::ColorAttachmentWrite, {}}}});
        const auto horizontal = graph.add_pass(
            {"bloom horizontal", {{ping, ResourceUsage::SampledRead, fragment},
                                     {pong, ResourceUsage::ColorAttachmentWrite, {}}}});
        const auto vertical = graph.add_pass(
            {"bloom vertical", {{pong, ResourceUsage::SampledRead, fragment},
                                   {ping, ResourceUsage::ColorAttachmentWrite, {}}}});
        return {ping, pong, {extract, horizontal, vertical}};
    }

    Result<void, GraphicsError> BloomPass::resize(const Math::Vec2u source_size) {
        if(source_size.x == 0 || source_size.y == 0)
            return Result<void, GraphicsError>::failure(
                {"Bloom source size must be greater than zero"});
        const Math::Vec2u size = source_size / 2u + source_size % 2u;
        if(m_targets[0] && m_targets[0]->get_size() == size) {
            m_source_size = source_size;
            return Result<void, GraphicsError>::success();
        }
        std::array<std::shared_ptr<RenderTarget>, 2> candidates;
        for(auto& target : candidates) {
            auto candidate = RenderTarget::try_create_multi_target(
                m_device, *m_render_pass, size, m_frame_slots);
            if(!candidate)
                return Result<void, GraphicsError>::failure(candidate.error());
            target = std::move(candidate).value();
        }
        m_targets = std::move(candidates);
        m_source_size = source_size;
        for(auto& bindings : m_bindings)
            bindings = {};
        return Result<void, GraphicsError>::success();
    }

    void BloomPass::bind_resources(
        std::span<RenderGraph::Binding> bindings, const Passes& passes, const uint32_t slot) const {
        assert(
            passes.output.index < bindings.size() && passes.intermediate.index < bindings.size());
        assert(m_targets[0] && m_targets[1] && slot < m_frame_slots);
        bindings[passes.output.index] = m_targets[0]->get_color_view(slot)->get_image();
        bindings[passes.intermediate.index] = m_targets[1]->get_color_view(slot)->get_image();
    }

    std::shared_ptr<ImageView> BloomPass::get_output(const uint32_t slot) const {
        if(!m_targets[0] || slot >= m_frame_slots)
            return nullptr;
        return m_targets[0]->get_color_view(slot);
    }

    Result<void, GraphicsError> BloomPass::render(FrameScheduler& frames,
        const RenderGraph::PassId pass, const Passes& passes, const std::shared_ptr<ImageView>& hdr,
        const float threshold) {
        const auto found = std::ranges::find(passes.ids, pass);
        if(!frames.is_recording_frame() || &frames.get_device() != &m_device
            || frames.get_current_frame_slot_index() >= m_frame_slots || !m_targets[0] || !hdr
            || found == passes.ids.end() || !std::isfinite(threshold) || threshold < 0
            || threshold > 65504)
            return Result<void, GraphicsError>::failure(
                {"Invalid bloom frame, input, pass or threshold"});
        const auto& info = hdr->get_image()->get_info();
        if(&hdr->get_image()->get_device() != &m_device || info.extent.x != m_source_size.x
            || info.extent.y != m_source_size.y || info.format != Format::R16G16B16A16_SFLOAT
            || !(info.usage & ImageUsage::Sampled))
            return Result<void, GraphicsError>::failure({"Invalid bloom source image"});
        const auto mode = static_cast<uint32_t>(found - passes.ids.begin());
        const auto slot = frames.get_current_frame_slot_index();
        auto input = hdr;
        auto target = m_targets[0];
        if(mode == 1) {
            input = m_targets[0]->get_color_view(slot);
            target = m_targets[1];
        } else if(mode == 2) {
            input = m_targets[1]->get_color_view(slot);
        }
        auto& binding = m_bindings[slot][mode];
        if(!binding || binding->images.front() != input) {
            auto candidate = SampledImageBinding::create(m_device, {input}, m_layout, m_sampler);
            if(!candidate)
                return Result<void, GraphicsError>::failure(candidate.error());
            binding = std::move(candidate).value();
        }
        frames.retain_current_frame_resource(binding);
        frames.retain_current_frame_resource(m_pipeline);
        frames.retain_current_frame_resource(m_render_pass);
        frames.retain_current_frame_resource(target);
        auto& command = frames.get_current_command_buffer();
        target->begin_render_target(command, slot);
        const auto size = target->get_size();
        command.set_viewport(vk::Viewport(0, 0, float(size.x), float(size.y), 0, 1));
        command.set_scissor(Graphics::get_scissor(float(size.x), float(size.y)));
        command.bind_pipeline(*m_pipeline);
        command.bind_descriptor_sets(*m_pipeline->get_layout(), std::span(&binding->descriptor, 1));
        struct Parameters {
            float threshold;
            uint32_t mode;
        };
        static_assert(sizeof(Parameters) == 8);
        const Parameters parameters{threshold, mode};
        command.push_constants(*m_pipeline->get_layout(), Flags<ShaderStage>(ShaderStage::Fragment),
            0, &parameters, sizeof(parameters));
        command.draw(3);
        target->end_render_target(command);
        return Result<void, GraphicsError>::success();
    }
}
