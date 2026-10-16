#include "render/passes/depth_pass.h"

#include "asset/data/mesh_data.h"
#include "graphics/device.h"
#include "graphics/frame_buffer.h"
#include "graphics/pipeline/pipeline.h"
#include "graphics/pipeline/shader.h"
#include "graphics/pipeline/vertex_description.h"
#include "graphics/render_pass.h"
#include "graphics/resource/image_view.h"
#include "graphics/resource/buffer.h"
#include "render/frame_scheduler.h"
#include "render/render_target.h"
#include "render/resource/mesh.h"
#include "render/scene/draw_order.h"
#include "depth_vert.h"
#include "depth_frag.h"
#include "depth_mask_frag.h"
#include "graphics/resource/sampler.h"
#include "render/material/material.h"
#include "render/resource/texture.h"
#include "scene/material_parameters.h"
#include "common/scope_exit.h"
#include <tuple>

#include <algorithm>
#include <cmath>

namespace Comet {
    DepthPass::DepthPass(Device& device, const uint32_t frame_slots,
        std::shared_ptr<RenderPass> pass, std::shared_ptr<RenderTarget> target,
        std::shared_ptr<Pipeline> pipeline)
        : m_device(device), m_frame_slots(frame_slots), m_render_pass(std::move(pass)),
          m_target(std::move(target)), m_instances(frame_slots) {
        m_pipelines[0] = std::move(pipeline);
    }

    Result<std::unique_ptr<DepthPass>, GraphicsError> DepthPass::create(
        Device& device, const uint32_t frame_slots, Math::Vec2u size, uint32_t view_count) {
        using Creation = Result<std::unique_ptr<DepthPass>, GraphicsError>;
        if(frame_slots == 0 || view_count == 0)
            return Creation::failure({"Depth pass requires frame slots"});
        const auto support = device.query_format_support(Format::D32_SFLOAT);
        if(!support.depth_attachment || !support.sampled)
            return Creation::failure({"D32 geometry depth sampling is unsupported"});
        auto depth = Attachment::get_depth_attachment(Format::D32_SFLOAT);
        depth.description.initial_layout = ImageLayout::DepthStencilAttachmentOptimal;
        depth.description.store_op = AttachmentStoreOp::Store;
        depth.usage |= ImageUsage::Sampled;
        const RenderSubPass subpass{
            .depth_stencil_attachments = {SubpassDepthStencilAttachment(0)}};
        auto pass = RenderPass::create(device, {depth}, {subpass});
        if(!pass)
            return Creation::failure(pass.error());
        auto target =
            RenderTarget::try_create_multi_target(device, *pass.value(), size, frame_slots);
        if(!target)
            return Creation::failure(target.error());
        auto vertex = Shader::create(device, "geometry depth vertex", DEPTH_VERT);
        if(!vertex)
            return Creation::failure(vertex.error());
        auto fragment = Shader::create(device, "geometry depth fragment", DEPTH_FRAG);
        if(!fragment)
            return Creation::failure(fragment.error());
        ShaderLayout layout;
        VertexInputDescription input;
        input.add_binding(0, sizeof(MeshVertex), VertexInputRate::Vertex);
        input.add_attribute(0, 0, Format::R32G32B32_SFLOAT, offsetof(MeshVertex, position));
        input.add_attribute(1, 0, Format::R32G32_SFLOAT, offsetof(MeshVertex, texcoord));
        InstanceBuffer::describe(input);
        PipelineConfig config;
        config.set_vertex_input_state(input);
        config.set_dynamic_state({DynamicState::Viewport, DynamicState::Scissor});
        config.enable_depth_test();
        config.rasterization_state.cull_mode = CullMode::Back;
        PipelineManager pipelines(device, *pass.value());
        auto pipeline = pipelines.create_pipeline(
            "geometry depth", layout, config, vertex.value(), fragment.value());
        if(!pipeline)
            return Creation::failure(pipeline.error());
        auto result = std::unique_ptr<DepthPass>(new DepthPass(device, frame_slots,
            std::move(pass).value(), std::move(target).value(), std::move(pipeline).value()));
        result->m_view_geometry = std::vector<RenderGeometry>(view_count);
        for(unsigned orientation = 1; orientation < 4; ++orientation) {
            config.rasterization_state.cull_mode =
                (orientation & 1) ? CullMode::None : CullMode::Back;
            config.rasterization_state.front_face =
                (orientation & 2) ? FrontFace::CCW : FrontFace::CW;
            auto created = pipelines.create_pipeline(
                "geometry depth", layout, config, vertex.value(), fragment.value());
            if(!created)
                return Creation::failure(created.error());
            result->m_pipelines[orientation] = std::move(created).value();
        }
        DescriptorSetLayoutBindings bindings;
        bindings.add_binding(
            0, DescriptorType::CombinedImageSampler, Flags<ShaderStage>(ShaderStage::Fragment));
        auto mask_layout = DescriptorSetLayout::create(device, bindings);
        if(!mask_layout)
            return Creation::failure(mask_layout.error());
        result->m_mask_layout = std::move(mask_layout).value();
        auto sampler = Sampler::create(device);
        if(!sampler)
            return Creation::failure(sampler.error());
        result->m_mask_sampler = std::move(sampler).value();
        auto mask_fragment = Shader::create(device, "depth mask fragment", DEPTH_MASK_FRAG);
        if(!mask_fragment)
            return Creation::failure(mask_fragment.error());
        layout.descriptor_set_layouts = {result->m_mask_layout};
        layout.push_constants = {
            std::make_shared<PushConstantRange>(ShaderStage::Fragment, 0, sizeof(float) * 2)};
        for(unsigned orientation = 0; orientation < 4; ++orientation) {
            config.rasterization_state.cull_mode =
                (orientation & 1) ? CullMode::None : CullMode::Back;
            config.rasterization_state.front_face =
                (orientation & 2) ? FrontFace::CCW : FrontFace::CW;
            auto masked = pipelines.create_pipeline(
                "geometry depth mask", layout, config, vertex.value(), mask_fragment.value());
            if(!masked)
                return Creation::failure(masked.error());
            result->m_mask_pipelines[orientation] = std::move(masked).value();
        }
        return Creation::success(std::move(result));
    }

    std::shared_ptr<ImageView> DepthPass::get_depth_view(const uint32_t slot) const {
        return m_target->get_framebuffer(slot)->get_attachments().front();
    }

    Result<void, GraphicsError> DepthPass::resize(Math::Vec2u size) {
        if(m_target->get_size() == size)
            return Result<void, GraphicsError>::success();
        auto target =
            RenderTarget::try_create_multi_target(m_device, *m_render_pass, size, m_frame_slots);
        if(!target)
            return Result<void, GraphicsError>::failure(target.error());
        m_target = std::move(target).value();
        return Result<void, GraphicsError>::success();
    }

    Result<std::shared_ptr<DepthPass::MaskBinding>, GraphicsError> DepthPass::prepare_mask(
        const std::shared_ptr<Texture>& texture) {
        using Preparation = Result<std::shared_ptr<MaskBinding>, GraphicsError>;
        auto& cached = m_masks[texture.get()];
        cached.used = true;
        if(cached.binding)
            return Preparation::success(cached.binding);
        DescriptorPoolSizes sizes;
        sizes.add_pool_size(DescriptorType::CombinedImageSampler, 1);
        auto pool = DescriptorPool::create(m_device, 1, sizes);
        if(!pool)
            return Preparation::failure(pool.error());
        auto sets = pool.value()->allocate_descriptor_set(*m_mask_layout, 1);
        if(!sets)
            return Preparation::failure(sets.error());
        const auto& sampler = texture->get_sampler() ? texture->get_sampler() : m_mask_sampler;
        auto binding = std::make_shared<MaskBinding>(MaskBinding{
            texture, sampler, m_mask_layout, std::move(pool).value(), sets.value().front()});
        const std::vector<DescriptorSet::ImageSamplerWrite> images{
            {0, *texture->get_image_view(), *sampler}};
        binding->descriptor.update(m_device, {}, images);
        cached.binding = binding;
        return Preparation::success(std::move(binding));
    }

    Result<void, GraphicsError> DepthPass::prepare_draws(
        std::span<const View> views, const RenderGeometry& geometry) {
        using Preparation = Result<void, GraphicsError>;
        for(auto& [texture, cached] : m_masks)
            cached.used = false;
        const ScopeExit collect_masks(
            [&] { std::erase_if(m_masks, [](const auto& entry) { return !entry.second.used; }); });
        for(unsigned view_index = 0; view_index < views.size(); ++view_index) {
            const auto& view = views[view_index];
            if(!view.enabled)
                continue;
            const auto& matrix = view.view_projection;
            auto& view_geometry = m_view_geometry[view_index];
            const auto& selected = view_geometry.prepare_view(geometry, matrix, true);
            m_statistics.coarse_culled_items += view_geometry.get_view_statistics().culled_items;
            m_statistics.lod_selections += view_geometry.get_view_statistics().lod_selections;
            for(const auto& prepared : selected.get_items()) {
                if(!prepared.world_bounds)
                    continue;
                const auto& input = prepared.source->material;
                const auto& source = input.resource;
                const auto surface = source ? source->get_surface() : MaterialSurface{};
                if(surface.alpha_mode == AlphaMode::Blend)
                    continue;
                DrawItem draw{prepared.source, {}, 1, 0.5f,
                    unsigned(surface.double_sided)
                        | (unsigned(DrawOrder::reverses_winding(*prepared.source)) << 1),
                    view_index};
                if(surface.alpha_mode == AlphaMode::Mask) {
                    const std::string property =
                        source->get_template_name() == "pbr" ? "base_color" : "color";
                    auto color = source->get_vector_property(property).value_or(Math::Vec4(1));
                    if(input.overrides) {
                        const auto value = input.overrides->vector_properties.find(property);
                        if(value != input.overrides->vector_properties.end())
                            color = value->second;
                    }
                    auto texture = source->get_texture_property("base_color_texture");
                    if(!texture) {
                        if(std::clamp(color.a, 0.0f, 1.0f) < surface.alpha_cutoff)
                            continue;
                    } else {
                        auto binding = prepare_mask(texture);
                        if(!binding)
                            return Preparation::failure(binding.error());
                        draw.mask = std::move(binding).value();
                        draw.alpha = color.a;
                        draw.cutoff = surface.alpha_cutoff;
                    }
                }
                m_draw_queue.push_back(std::move(draw));
            }
        }
        DrawOrder::sort_if_needed(m_draw_queue, [](const DrawItem& a, const DrawItem& b) {
            return std::tuple(
                       a.view, a.orientation, a.mask.get(), a.alpha, a.cutoff, a.item->mesh.get())
                   < std::tuple(
                       b.view, b.orientation, b.mask.get(), b.alpha, b.cutoff, b.item->mesh.get());
        });
        m_transforms.reserve(m_draw_queue.size());
        for(const auto& draw : m_draw_queue)
            m_transforms.push_back(views[draw.view].view_projection * draw.item->model_matrix);
        return Preparation::success();
    }

    Result<std::vector<QueueSemaphoreSubmit>, GraphicsError> DepthPass::render(
        FrameScheduler& frames, std::span<const View> views, const RenderGeometry& geometry) {
        using Draw = Result<std::vector<QueueSemaphoreSubmit>, GraphicsError>;
        if(!frames.is_recording_frame() || &frames.get_device() != &m_device
            || frames.get_current_frame_slot_index() >= m_frame_slots
            || views.size() != m_view_geometry.size())
            return Draw::failure({"Invalid depth pass frame"});
        m_statistics = {};
        m_draw_queue.clear();
        m_transforms.clear();
        const ScopeExit release_draws([&] {
            m_draw_queue.clear();
            for(auto& view : m_view_geometry)
                view.clear_frame();
        });
        if(auto prepared = prepare_draws(views, geometry); !prepared)
            return Draw::failure(prepared.error());
        auto& instances = m_instances.at(frames.get_current_frame_slot_index());
        auto uploaded = instances.upload(m_device, m_transforms);
        if(!uploaded) {
            m_draw_queue.clear();
            return Draw::failure(uploaded.error());
        }
        m_statistics.instance_upload_bytes = uploaded.value();
        auto& command = frames.get_current_command_buffer();
        frames.retain_current_frame_resource(m_render_pass);
        frames.retain_current_frame_resource(m_target);

        m_target->begin_render_target(command, frames.get_current_frame_slot_index());

        std::vector<QueueSemaphoreSubmit> waits;
        if(!m_draw_queue.empty()) {

            instances.bind(command);
            frames.retain_current_frame_resource(instances.get_buffer());
            for(size_t first = 0; first < m_draw_queue.size();) {
                const auto& draw = m_draw_queue[first];
                const auto& mesh = draw.item->mesh;
                const auto& rect = views[draw.view].atlas_rect;
                const auto size = m_target->get_size();
                const uint32_t x = uint32_t(std::round(rect.z * size.x));
                const uint32_t y = uint32_t(std::round(rect.w * size.y));
                const uint32_t width = uint32_t(std::round(rect.x * size.x));
                const uint32_t height = uint32_t(std::round(rect.y * size.y));
                command.set_viewport(
                    vk::Viewport(float(x), float(y), float(width), float(height), 0, 1));
                command.set_scissor(vk::Rect2D({int32_t(x), int32_t(y)}, {width, height}));
                const auto& pipeline =
                    draw.mask ? m_mask_pipelines[draw.orientation] : m_pipelines[draw.orientation];
                command.bind_pipeline(*pipeline);
                frames.retain_current_frame_resource(pipeline);
                if(draw.mask) {
                    const std::array sets{draw.mask->descriptor};
                    command.bind_descriptor_sets(*pipeline->get_layout(), sets);
                    const float parameters[]{draw.alpha, draw.cutoff};
                    command.push_constants(*pipeline->get_layout(),
                        Flags<ShaderStage>(ShaderStage::Fragment), 0, parameters,
                        sizeof(parameters));
                    frames.retain_current_frame_resource(draw.mask);
                    const auto& ready = draw.mask->texture->get_ready_completion();
                    if(ready.is_valid() && !ready.is_complete())
                        merge_semaphore_wait(
                            waits, QueueSemaphoreSubmit(
                                       ready, Flags<PipelineStage>(PipelineStage::FragmentShader)));
                }
                size_t end = first + 1;
                while(end < m_draw_queue.size()) {
                    const auto& next = m_draw_queue[end];
                    if(next.view != draw.view || next.item->mesh != mesh
                        || next.orientation != draw.orientation || next.mask != draw.mask
                        || next.alpha != draw.alpha || next.cutoff != draw.cutoff)
                        break;
                    ++end;
                }
                mesh->bind(command);
                frames.retain_current_frame_resource(mesh);
                const auto& ready = mesh->get_ready_completion();
                if(ready.is_valid() && !ready.is_complete())
                    merge_semaphore_wait(
                        waits, QueueSemaphoreSubmit(
                                   ready, Flags<PipelineStage>(PipelineStage::VertexInput)));
                const auto count = static_cast<uint32_t>(end - first);
                mesh->draw(command, count, static_cast<uint32_t>(first));
                ++m_statistics.draw_calls;
                m_statistics.drawn_instances += count;
                m_statistics.drawn_triangles += uint64_t(mesh->get_triangle_count()) * count;
                first = end;
            }
        }
        m_target->end_render_target(command);
        m_draw_queue.clear();
        return Draw::success(std::move(waits));
    }
}
