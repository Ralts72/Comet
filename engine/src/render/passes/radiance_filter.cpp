#include "render/passes/radiance_filter.h"
#include "graphics/attachment.h"
#include "graphics/device.h"
#include "graphics/pipeline/pipeline.h"
#include "graphics/pipeline/shader.h"
#include "graphics/render_pass.h"
#include "graphics/resource/image_view.h"
#include "graphics/resource/image.h"
#include "graphics/resource/sampler.h"
#include "graphics/frame_buffer.h"
#include "graphics/convert.h"
#include "render/frame_scheduler.h"
#include "render/render_target.h"
#include "render/render_graph.h"
#include "render/resource/sampled_image_binding.h"
#include "display_vert.h"
#include "radiance_filter_frag.h"
#include <algorithm>

namespace Comet {
    struct RadianceFilter::History {
        std::shared_ptr<RenderTarget> target;
        std::vector<std::array<std::shared_ptr<SampledImageBinding>, 2>> bindings;
        std::array<bool, 2> initialized{};
    };
    Result<std::unique_ptr<RadianceFilter>, GraphicsError> RadianceFilter::create(Device& device) {
        using Created = Result<std::unique_ptr<RadianceFilter>, GraphicsError>;
        auto result = std::unique_ptr<RadianceFilter>(new RadianceFilter(device));
        auto color = Attachment::get_color_attachment(Format::R32G32B32A32_SFLOAT);
        color.description.store_op = AttachmentStoreOp::Store;
        color.description.initial_layout = ImageLayout::ColorAttachmentOptimal;
        color.description.final_layout = ImageLayout::ColorAttachmentOptimal;
        color.usage |= ImageUsage::Sampled;
        color.usage |= ImageUsage::CopySrc;
        auto guide = color;
        guide.description.format = Format::R32G32B32A32_SFLOAT;
        auto pass = RenderPass::create(device, {color, guide},
            {RenderSubPass{
                .color_attachments = {SubpassColorAttachment(0), SubpassColorAttachment(1)}}});
        if(!pass)
            return Created::failure(pass.error());
        result->m_pass = std::move(pass).value();
        DescriptorSetLayoutBindings bindings;
        for(uint32_t index = 0; index < 5; ++index)
            bindings.add_binding(index, DescriptorType::CombinedImageSampler,
                Flags<ShaderStage>(ShaderStage::Fragment));
        auto layout = DescriptorSetLayout::create(device, bindings);
        if(!layout)
            return Created::failure(layout.error());
        result->m_layout = std::move(layout).value();
        auto sampler =
            Sampler::create(device, {.mag_filter = Filter::Nearest,
                                        .min_filter = Filter::Nearest,
                                        .address_mode_u = SamplerAddressMode::ClampToEdge,
                                        .address_mode_v = SamplerAddressMode::ClampToEdge,
                                        .address_mode_w = SamplerAddressMode::ClampToEdge});
        if(!sampler)
            return Created::failure(sampler.error());
        result->m_sampler = std::move(sampler).value();
        auto vertex = Shader::create(device, "fullscreen", DISPLAY_VERT);
        if(!vertex)
            return Created::failure(vertex.error());
        auto fragment = Shader::create(device, "radiance filter", RADIANCE_FILTER_FRAG);
        if(!fragment)
            return Created::failure(fragment.error());
        ShaderLayout shader_layout;
        shader_layout.descriptor_set_layouts = {result->m_layout};
        shader_layout.push_constants = {
            std::make_shared<PushConstantRange>(ShaderStage::Fragment, 0, 48)};
        PipelineConfig config;
        config.set_dynamic_state({DynamicState::Viewport, DynamicState::Scissor});
        PipelineManager pipelines(device, *result->m_pass);
        auto pipeline = pipelines.create_pipeline(
            "radiance filter", shader_layout, config, vertex.value(), fragment.value());
        if(!pipeline)
            return Created::failure(pipeline.error());
        result->m_pipeline = std::move(pipeline).value();
        return Created::success(std::move(result));
    }

    Result<void, GraphicsError> RadianceFilter::render(FrameScheduler& frames,
        const std::shared_ptr<ImageView>& color, const std::shared_ptr<ImageView>& guide,
        const std::shared_ptr<ImageView>& reprojection, const ViewProjectMatrix& camera,
        const ViewProjectMatrix& previous, float history_weight, bool valid) {
        const auto extent = color->get_image()->get_info().extent;
        const Math::Vec2u size(extent.x, extent.y);
        if(!m_history || m_history->target->get_size() != size
            || m_serial == frames.get_current_frame_serial()) {
            auto target = RenderTarget::try_create_multi_target(m_device, *m_pass, size, 2);
            if(!target)
                return Result<void, GraphicsError>::failure(target.error());
            auto next = std::make_shared<History>();
            next->target = std::move(target).value();
            next->bindings.resize(frames.get_frame_slot_count());
            m_history = std::move(next);
            m_serial = 0;
            m_index = 1;
        }
        valid = valid && m_serial + 1 == frames.get_current_frame_serial()
                && m_history->initialized[m_index];
        const uint32_t next = m_index ^ 1;
        const auto attachments = m_history->target->get_framebuffer(next)->get_attachments();
        const auto old = m_history->target->get_framebuffer(m_index)->get_attachments();
        const std::vector images{
            color, guide, reprojection, valid ? old[0] : color, valid ? old[1] : guide};
        auto& binding = m_history->bindings.at(frames.get_current_frame_slot_index())[next];
        if(!binding || binding->images != images) {
            auto created = SampledImageBinding::create(m_device, images, m_layout, m_sampler);
            if(!created)
                return Result<void, GraphicsError>::failure(created.error());
            binding = std::move(created).value();
        }
        frames.retain_current_frame_resource(m_history);
        frames.retain_current_frame_resource(binding);
        frames.retain_current_frame_resource(m_pipeline);
        frames.retain_current_frame_resource(m_pass);
        RenderGraph graph;
        const auto range = ImageSubresourceRange{.aspects = Flags<ImageAspect>(ImageAspect::Color)};
        const auto sampled = *resolve_image_state(
            ResourceUsage::SampledRead, range, Flags<PipelineStage>(PipelineStage::FragmentShader));
        const auto result_color = graph.import_image("filtered radiance",
            m_history->initialized[next] ? sampled : ImageState{.subresources = range});
        const auto result_guide = graph.import_image("filter guide",
            m_history->initialized[next] ? sampled : ImageState{.subresources = range});
        std::vector<RenderGraph::Binding> resources{
            attachments[0]->get_image(), attachments[1]->get_image()};
        RenderGraph::Pass filter{"spatial temporal radiance",
            {{result_color, ResourceUsage::ColorAttachmentWrite, {}},
                {result_guide, ResourceUsage::ColorAttachmentWrite, {}}}};
        std::vector<std::shared_ptr<Image>> declared;
        for(size_t index = 0; index < images.size(); ++index) {
            const auto& input = images[index];
            // 首帧历史描述符使用当前输入占位，同一物理图像只声明一次。
            if(std::ranges::find(declared, input->get_image()) != declared.end())
                continue;
            declared.push_back(input->get_image());
            const auto id = graph.import_image("filter input " + std::to_string(index), sampled);
            resources.emplace_back(input->get_image());
            filter.uses.push_back({id, ResourceUsage::SampledRead,
                Flags<PipelineStage>(PipelineStage::FragmentShader)});
        }
        const auto filter_id = graph.add_pass(std::move(filter));
        graph.add_pass(
            {"filter output", {{result_color, ResourceUsage::SampledRead,
                                   Flags<PipelineStage>(PipelineStage::FragmentShader)},
                                  {result_guide, ResourceUsage::SampledRead,
                                      Flags<PipelineStage>(PipelineStage::FragmentShader)}}});
        auto plan = graph.compile();
        if(!plan)
            return Result<void, GraphicsError>::failure({plan.error()});
        const auto recorded = plan.value().record(
            frames, resources, [&](RenderGraph::PassId pass, CommandBuffer& command) {
                if(pass != filter_id)
                    return Result<void, GraphicsError>::success();
                m_history->target->begin_render_target(command, next);
                command.set_viewport(vk::Viewport(0, 0, float(size.x), float(size.y), 0, 1));
                command.set_scissor(Graphics::get_scissor(float(size.x), float(size.y)));
                command.bind_pipeline(*m_pipeline);
                command.bind_descriptor_sets(
                    *m_pipeline->get_layout(), std::span(&binding->descriptor, 1));
                struct Parameters {
                    Math::Vec4 projection, previous_projection;
                    float weight;
                    uint32_t valid;
                    std::array<uint32_t, 2> padding{};
                } parameters{camera.depth_parameters(), previous.depth_parameters(), history_weight,
                    valid ? 1u : 0u};
                static_assert(sizeof(Parameters) == 48);
                command.push_constants(*m_pipeline->get_layout(),
                    Flags<ShaderStage>(ShaderStage::Fragment), 0, &parameters, sizeof(parameters));
                command.draw(3);
                m_history->target->end_render_target(command);
                return Result<void, GraphicsError>::success();
            });
        if(recorded) {
            m_history->initialized[next] = true;
            m_index = next;
            m_serial = frames.get_current_frame_serial();
        }
        return recorded;
    }
    std::shared_ptr<ImageView> RadianceFilter::output() const {
        return m_history ? m_history->target->get_framebuffer(m_index)->get_attachments().front()
                         : nullptr;
    }
    void RadianceFilter::reset() {
        m_history.reset();
        m_serial = 0;
    }
}
