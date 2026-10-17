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
#include "motion_vert.h"
#include "motion_frag.h"
#include "motion_mask_frag.h"
#include "outline_mask_frag.h"
#include "outline_frag.h"
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

    Result<std::unique_ptr<DepthPass>, GraphicsError> DepthPass::create(Device& device,
        const uint32_t frame_slots, Math::Vec2u size, uint32_t view_count, Output output) {
        const bool motion_vectors = output == Output::Motion;
        const bool outline = output == Output::Outline;
        using Creation = Result<std::unique_ptr<DepthPass>, GraphicsError>;
        if(frame_slots == 0 || view_count == 0)
            return Creation::failure({"Depth pass requires frame slots"});
        if(motion_vectors && view_count != 1)
            return Creation::failure({"Motion vectors require one camera view"});
        const auto support = device.query_format_support(Format::D32_SFLOAT);
        if(!support.depth_attachment || !support.sampled)
            return Creation::failure({"D32 geometry depth sampling is unsupported"});
        auto depth = Attachment::get_depth_attachment(Format::D32_SFLOAT);
        depth.description.initial_layout = ImageLayout::DepthStencilAttachmentOptimal;
        depth.description.store_op = AttachmentStoreOp::Store;
        depth.usage |= ImageUsage::Sampled;
        RenderSubPass subpass{.depth_stencil_attachments = {SubpassDepthStencilAttachment(0)}};
        std::vector<Attachment> attachments{depth};
        if(motion_vectors || outline) {
            auto motion = Attachment::get_color_attachment(Format::R16G16B16A16_SFLOAT);
            motion.description.initial_layout = ImageLayout::ColorAttachmentOptimal;
            motion.description.final_layout = ImageLayout::ColorAttachmentOptimal;
            motion.description.store_op = AttachmentStoreOp::Store;
            motion.usage |= ImageUsage::Sampled;
            motion.usage |= ImageUsage::CopySrc;
            attachments.push_back(motion);
            subpass.color_attachments = {SubpassColorAttachment(1)};
            if(outline) {
                attachments.push_back(motion);
                subpass.color_attachments.push_back(SubpassColorAttachment(2));
            }
        }
        auto pass = RenderPass::create(device, attachments, {subpass});
        if(!pass)
            return Creation::failure(pass.error());
        auto target =
            RenderTarget::try_create_multi_target(device, *pass.value(), size, frame_slots);
        if(!target)
            return Creation::failure(target.error());
        auto vertex = Shader::create(device, "geometry depth vertex",
            motion_vectors ? std::span<const uint32_t>(MOTION_VERT)
                           : std::span<const uint32_t>(DEPTH_VERT));
        if(!vertex)
            return Creation::failure(vertex.error());
        auto fragment = Shader::create(device, "geometry depth fragment",
            outline ? std::span<const uint32_t>(OUTLINE_FRAG)
                    : (motion_vectors ? std::span<const uint32_t>(MOTION_FRAG)
                                      : std::span<const uint32_t>(DEPTH_FRAG)));
        if(!fragment)
            return Creation::failure(fragment.error());
        ShaderLayout layout;
        if(outline)
            layout.push_constants = {
                std::make_shared<PushConstantRange>(ShaderStage::Fragment, 0, 48)};
        VertexInputDescription input;
        input.add_binding(0, sizeof(MeshVertex), VertexInputRate::Vertex);
        input.add_attribute(0, 0, Format::R32G32B32_SFLOAT, offsetof(MeshVertex, position));
        input.add_attribute(1, 0, Format::R32G32_SFLOAT, offsetof(MeshVertex, texcoord));
        InstanceBuffer::describe(input);
        if(motion_vectors) {
            InstanceBuffer::describe(input, 2, 7);
            input.add_binding(3, sizeof(MeshVertex), VertexInputRate::Vertex);
            input.add_attribute(11, 3, Format::R32G32B32_SFLOAT, offsetof(MeshVertex, position));
            layout.push_constants = {
                std::make_shared<PushConstantRange>(ShaderStage::Fragment, 0, 32)};
        }
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
        result->m_motion_vectors = motion_vectors;
        result->m_outline = outline;
        if(motion_vectors)
            result->m_previous_instances.resize(frame_slots);
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
        auto mask_fragment = Shader::create(device, "depth mask fragment",
            outline ? std::span<const uint32_t>(OUTLINE_MASK_FRAG)
                    : (motion_vectors ? std::span<const uint32_t>(MOTION_MASK_FRAG)
                                      : std::span<const uint32_t>(DEPTH_MASK_FRAG)));
        if(!mask_fragment)
            return Creation::failure(mask_fragment.error());
        layout.descriptor_set_layouts = {result->m_mask_layout};
        layout.push_constants = {std::make_shared<PushConstantRange>(
            ShaderStage::Fragment, 0, outline ? 48 : (motion_vectors ? 32 : sizeof(float) * 2))};
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

    std::shared_ptr<ImageView> DepthPass::get_motion_view(const uint32_t slot) const {
        return m_motion_vectors ? m_target->get_framebuffer(slot)->get_attachments()[1] : nullptr;
    }

    std::shared_ptr<ImageView> DepthPass::get_color_view(uint32_t slot, uint32_t index) const {
        return m_target->get_framebuffer(slot)->get_attachments()[index + 1];
    }

    Result<void, GraphicsError> DepthPass::resize(Math::Vec2u size) {
        if(m_target->get_size() == size)
            return Result<void, GraphicsError>::success();
        auto target =
            RenderTarget::try_create_multi_target(m_device, *m_render_pass, size, m_frame_slots);
        if(!target)
            return Result<void, GraphicsError>::failure(target.error());
        m_target = std::move(target).value();
        m_motion_history.clear();
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
        std::span<const View> views, const RenderGeometry& geometry, LodSelection lod) {
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
            const bool select_lod = lod == LodSelection::PerView;
            const auto& selected =
                select_lod ? view_geometry.prepare_view(geometry, matrix, true) : geometry;
            const auto frustum = select_lod ? std::nullopt : Frustum::from_view_projection(matrix);
            if(select_lod) {
                m_statistics.coarse_culled_items +=
                    view_geometry.get_view_statistics().culled_items;
                m_statistics.lod_selections += view_geometry.get_view_statistics().lod_selections;
            }
            for(const auto& prepared : selected.get_items()) {
                if(m_outline && !prepared.source->outline.active())
                    continue;
                if(!prepared.world_bounds)
                    continue;
                if(frustum && !frustum->intersects(*prepared.world_bounds)) {
                    ++m_statistics.coarse_culled_items;
                    continue;
                }
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
        if(m_motion_vectors) {
            m_previous_transforms.clear();
            for(auto& [id, history] : m_motion_history)
                history.used = false;
            for(const auto& draw : m_draw_queue) {
                const auto& item = *draw.item;
                const auto found = m_motion_history.find(item.entity_id);
                const auto& previous = views.front().previous_view_projection;
                const bool valid = previous && found != m_motion_history.end()
                                   && found->second.handle == item.mesh_handle
                                   && found->second.revision == item.mesh_revision
                                   && (item.mesh->m_deformed || found->second.mesh == item.mesh);
                m_previous_transforms.push_back(
                    valid ? *previous * found->second.model : Math::Mat4(0));
            }
        }
        return Preparation::success();
    }

    Result<std::vector<QueueSemaphoreSubmit>, GraphicsError> DepthPass::render(
        FrameScheduler& frames, std::span<const View> views, const RenderGeometry& geometry,
        LodSelection lod) {
        using Draw = Result<std::vector<QueueSemaphoreSubmit>, GraphicsError>;
        if(!frames.is_recording_frame() || &frames.get_device() != &m_device
            || frames.get_current_frame_slot_index() >= m_frame_slots
            || views.size() != m_view_geometry.size()
            || (lod == LodSelection::AlreadySelected && views.size() != 1))
            return Draw::failure({"Invalid depth pass frame"});
        m_statistics = {};
        m_draw_queue.clear();
        m_transforms.clear();
        if(m_motion_vectors && !views.front().previous_view_projection)
            m_motion_history.clear();
        const ScopeExit release_draws([&] {
            m_draw_queue.clear();
            for(auto& view : m_view_geometry)
                view.clear_frame();
        });
        if(auto prepared = prepare_draws(views, geometry, lod); !prepared)
            return Draw::failure(prepared.error());
        auto& instances = m_instances.at(frames.get_current_frame_slot_index());
        auto uploaded = instances.upload(m_device, m_transforms);
        if(!uploaded) {
            m_draw_queue.clear();
            return Draw::failure(uploaded.error());
        }
        m_statistics.instance_upload_bytes = uploaded.value();
        if(m_motion_vectors) {
            auto& previous = m_previous_instances.at(frames.get_current_frame_slot_index());
            auto written = previous.upload(m_device, m_previous_transforms);
            if(!written)
                return Draw::failure(written.error());
            m_statistics.instance_upload_bytes += written.value();
        }
        auto& command = frames.get_current_command_buffer();
        frames.retain_current_frame_resource(m_render_pass);
        frames.retain_current_frame_resource(m_target);

        if(m_outline)
            m_target->set_clear_value(ClearValue(Math::Vec4(0)));
        m_target->begin_render_target(command, frames.get_current_frame_slot_index());

        std::vector<QueueSemaphoreSubmit> waits;
        if(!m_draw_queue.empty()) {

            instances.bind(command);
            frames.retain_current_frame_resource(instances.get_buffer());
            if(m_motion_vectors) {
                auto& previous = m_previous_instances.at(frames.get_current_frame_slot_index());
                previous.bind(command, 2);
                frames.retain_current_frame_resource(previous.get_buffer());
            }
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
                if(m_motion_vectors) {
                    const auto history = m_motion_history.find(draw.item->entity_id);
                    auto vertices = mesh->m_vertex_buffer;
                    if(mesh->m_deformed && history != m_motion_history.end()
                        && history->second.vertices
                        && history->second.handle == draw.item->mesh_handle
                        && history->second.revision == draw.item->mesh_revision
                        && history->second.vertices->get_size()
                               == mesh->m_vertex_buffer->get_size())
                        vertices = history->second.vertices;
                    command.bind_vertex_buffer({*vertices, 0}, 3);
                    frames.retain_current_frame_resource(vertices);
                    command.push_constants(*pipeline->get_layout(),
                        Flags<ShaderStage>(ShaderStage::Fragment), 16,
                        &views.front().previous_projection, sizeof(Math::Vec4));
                }
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
                if(m_outline) {
                    const auto& style = draw.item->outline;
                    const std::array<Math::Vec4, 3> values{
                        Math::Vec4(draw.alpha, draw.cutoff, 0, 0), style.color,
                        Math::Vec4(style.width, style.fill, style.through_walls ? 1.f : 0.f, 0)};
                    command.push_constants(*pipeline->get_layout(),
                        Flags<ShaderStage>(ShaderStage::Fragment), 0, values.data(),
                        sizeof(values));
                }
                size_t end = first + 1;
                while(end < m_draw_queue.size()) {
                    const auto& next = m_draw_queue[end];
                    if((m_motion_vectors && mesh->m_deformed) || next.view != draw.view
                        || next.item->mesh != mesh || next.orientation != draw.orientation
                        || next.mask != draw.mask || next.alpha != draw.alpha
                        || next.cutoff != draw.cutoff
                        || (m_outline && next.item->outline != draw.item->outline))
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
        if(m_motion_vectors) {
            for(const auto& draw : m_draw_queue) {
                const auto& item = *draw.item;
                auto& history = m_motion_history[item.entity_id];
                if(item.mesh->m_deformed
                    && (!history.vertices || history.animation != item.animation
                        || history.handle != item.mesh_handle
                        || history.revision != item.mesh_revision
                        || history.vertices->get_size()
                               != item.mesh->m_vertex_buffer->get_size())) {
                    const auto& source = item.mesh->m_vertex_buffer;
                    if(!history.vertices || history.vertices->get_size() != source->get_size()) {
                        auto buffer = Buffer::try_create_gpu_buffer(m_device,
                            Flags<BufferUsage>(BufferUsage::Vertex) | BufferUsage::CopyDst,
                            source->get_size(), false, "previous skinned vertices");
                        if(!buffer)
                            return Draw::failure(buffer.error());
                        history.vertices = std::move(buffer).value();
                    }
                    const vk::MemoryBarrier before(
                        vk::AccessFlagBits::eVertexAttributeRead | vk::AccessFlagBits::eShaderWrite,
                        vk::AccessFlagBits::eTransferRead | vk::AccessFlagBits::eTransferWrite);
                    command.get().pipelineBarrier(vk::PipelineStageFlagBits::eVertexInput
                                                      | vk::PipelineStageFlagBits::eComputeShader,
                        vk::PipelineStageFlagBits::eTransfer, {}, before, {}, {});
                    command.get().copyBuffer(source->get(), history.vertices->get(),
                        vk::BufferCopy(0, 0, source->get_size()));
                    const vk::MemoryBarrier after(vk::AccessFlagBits::eTransferWrite,
                        vk::AccessFlagBits::eVertexAttributeRead);
                    command.get().pipelineBarrier(vk::PipelineStageFlagBits::eTransfer,
                        vk::PipelineStageFlagBits::eVertexInput, {}, after, {}, {});
                    frames.retain_current_frame_resource(history.vertices);
                }
                history.model = item.model_matrix;
                history.handle = item.mesh_handle;
                history.revision = item.mesh_revision;
                history.mesh = item.mesh;
                history.animation = item.animation;
                history.used = true;
            }
            std::erase_if(m_motion_history, [](const auto& entry) { return !entry.second.used; });
        }
        m_draw_queue.clear();
        return Draw::success(std::move(waits));
    }
}
