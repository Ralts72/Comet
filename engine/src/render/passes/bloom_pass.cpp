#include "render/passes/bloom_pass.h"

#include "graphics/device.h"
#include "graphics/convert.h"
#include "graphics/render_pass.h"
#include "graphics/frame_buffer.h"
#include "graphics/pipeline/pipeline.h"
#include "graphics/pipeline/shader.h"
#include "graphics/resource/image.h"
#include "graphics/resource/image_view.h"
#include "graphics/resource/sampler.h"
#include "render/frame_scheduler.h"
#include "render/resource/sampled_image_binding.h"
#include "bloom_vert.h"
#include "bloom_frag.h"

#include <algorithm>
#include <cmath>
#include <cassert>

namespace Comet {
    struct BloomPass::Resources {
        Passes passes;
        Math::Vec2u source_size;
        std::vector<std::array<std::shared_ptr<ImageView>, 3>> views;
        std::vector<std::array<std::shared_ptr<FrameBuffer>, 3>> framebuffers;
        std::vector<std::array<std::shared_ptr<SampledImageBinding>, 3>> bindings;
    };
    BloomPass::BloomPass(Device& device, const uint32_t frame_slots)
        : m_device(device), m_frame_slots(frame_slots) {}

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
        RenderGraph& graph, const RenderGraph::ResourceId hdr, const Math::Vec2u source_size) {
        const auto size = source_size / 2u + source_size % 2u;
        const RenderGraph::ImageDescription description{
            Format::R16G16B16A16_SFLOAT, size.x, size.y};
        const auto extracted = graph.create_image("bloom extract", description);
        const auto horizontal_output = graph.create_image("bloom horizontal", description);
        const auto output = graph.create_image("bloom output", description);
        const Flags<PipelineStage> fragment(PipelineStage::FragmentShader);
        const auto extract = graph.add_pass(
            {"bloom extract", {{hdr, ResourceUsage::SampledRead, fragment},
                                  {extracted, ResourceUsage::ColorAttachmentWrite, {}}}});
        const auto horizontal = graph.add_pass({"bloom horizontal",
            {{extracted, ResourceUsage::SampledRead, fragment},
                {horizontal_output, ResourceUsage::ColorAttachmentWrite, {}}}});
        const auto vertical = graph.add_pass(
            {"bloom vertical", {{horizontal_output, ResourceUsage::SampledRead, fragment},
                                   {output, ResourceUsage::ColorAttachmentWrite, {}}}});
        return {output, extracted, horizontal_output, {extract, horizontal, vertical}};
    }

    Result<std::shared_ptr<BloomPass::Resources>, GraphicsError> BloomPass::create_resources(
        const RenderGraph::Resources& resources, const Passes& passes,
        const Math::Vec2u source_size) const {
        using Creation = Result<std::shared_ptr<Resources>, GraphicsError>;
        if(source_size.x == 0 || source_size.y == 0)
            return Creation::failure({"Bloom source size must be greater than zero"});
        const Math::Vec2u size = source_size / 2u + source_size % 2u;
        const std::array ids{passes.extracted, passes.intermediate, passes.output};
        for(const auto id : ids)
            if(id.index >= resources.get_bindings(0).size())
                return Creation::failure({"Invalid Bloom logical target"});
        auto next = std::make_shared<Resources>();
        next->passes = passes;
        next->source_size = source_size;
        for(uint32_t slot = 0; slot < m_frame_slots; ++slot) {
            std::array<std::shared_ptr<ImageView>, 3> views;
            std::array<std::shared_ptr<FrameBuffer>, 3> framebuffers;
            const auto bindings = resources.get_bindings(slot);
            for(size_t index = 0; index < ids.size(); ++index) {
                const auto* image =
                    std::get_if<std::shared_ptr<Image>>(&bindings[ids[index].index]);
                if(!image || !*image || &(*image)->get_device() != &m_device
                    || (*image)->get_info().extent != Math::Vec3u(size.x, size.y, 1)
                    || (*image)->get_info().format != Format::R16G16B16A16_SFLOAT)
                    return Creation::failure({"Invalid Bloom target description"});
                auto view =
                    ImageView::try_create(m_device, *image, Flags<ImageAspect>(ImageAspect::Color));
                if(!view)
                    return Creation::failure(view.error());
                views[index] = std::move(view).value();
                auto framebuffer = FrameBuffer::try_create(
                    m_device, *m_render_pass, {views[index]}, size.x, size.y);
                if(!framebuffer)
                    return Creation::failure(framebuffer.error());
                framebuffers[index] = std::move(framebuffer).value();
            }
            next->views.push_back(std::move(views));
            next->framebuffers.push_back(std::move(framebuffers));
        }
        next->bindings.resize(m_frame_slots);
        return Creation::success(std::move(next));
    }

    std::shared_ptr<ImageView> BloomPass::output(const Resources& resources, const uint32_t slot) {
        if(slot >= resources.views.size())
            return nullptr;
        return resources.views[slot][2];
    }

    Result<void, GraphicsError> BloomPass::render(FrameScheduler& frames,
        const RenderGraph::PassId pass, const std::shared_ptr<Resources>& resources,
        const std::shared_ptr<ImageView>& hdr, const float threshold) {
        if(!resources)
            return Result<void, GraphicsError>::failure(
                {"Bloom requires prepared graph resources"});
        const auto& passes = resources->passes;
        const auto found = std::ranges::find(passes.ids, pass);
        if(!frames.is_recording_frame() || &frames.get_device() != &m_device
            || frames.get_current_frame_slot_index() >= resources->views.size() || !hdr
            || found == passes.ids.end() || !std::isfinite(threshold) || threshold < 0
            || threshold > 65504)
            return Result<void, GraphicsError>::failure(
                {"Invalid bloom frame, input, pass or threshold"});
        const auto& info = hdr->get_image()->get_info();
        if(&hdr->get_image()->get_device() != &m_device || info.extent.x != resources->source_size.x
            || info.extent.y != resources->source_size.y
            || info.format != Format::R16G16B16A16_SFLOAT || !(info.usage & ImageUsage::Sampled))
            return Result<void, GraphicsError>::failure({"Invalid bloom source image"});
        const auto mode = static_cast<uint32_t>(found - passes.ids.begin());
        const auto slot = frames.get_current_frame_slot_index();
        auto input = hdr;
        if(mode > 0)
            input = resources->views[slot][mode - 1];
        auto& binding = resources->bindings[slot][mode];
        if(!binding || binding->images.front() != input) {
            auto candidate = SampledImageBinding::create(m_device, {input}, m_layout, m_sampler);
            if(!candidate)
                return Result<void, GraphicsError>::failure(candidate.error());
            binding = std::move(candidate).value();
        }
        frames.retain_current_frame_resource(binding);
        frames.retain_current_frame_resource(m_pipeline);
        frames.retain_current_frame_resource(m_render_pass);
        frames.retain_current_frame_resource(resources);
        auto& command = frames.get_current_command_buffer();
        command.begin_render_pass(
            *m_render_pass, *resources->framebuffers[slot][mode], {ClearValue(Math::Vec4(0))});
        const auto size = resources->source_size / 2u + resources->source_size % 2u;
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
        command.end_render_pass();
        return Result<void, GraphicsError>::success();
    }
}
