#include "render/passes/volumetric_fog_pass.h"
#include "render/passes/particle_pass.h"
#include "render/passes/gpu_particle_simulation.h"

#include "graphics/command/command_buffer.h"
#include "graphics/pipeline/pipeline.h"
#include "graphics/pipeline/shader.h"
#include "graphics/pipeline/vertex_description.h"
#include "graphics/resource/buffer.h"
#include "render/frame_scheduler.h"
#include "asset/data/texture_data.h"
#include "graphics/resource/image.h"
#include "graphics/resource/image_view.h"
#include "graphics/resource/sampler.h"
#include "render/resource/render_resources.h"
#include "render/resource/sampled_image_binding.h"
#include "render/resource/texture.h"
#include "render/scene/render_submission.h"
#include "render/scene/draw_order.h"

#include "billboard_frag.h"
#include "billboard_vert.h"

#include <bit>
#include <cstddef>
#include <utility>
#include <unordered_set>
#include <algorithm>

namespace Comet {
    static_assert(sizeof(ParticleInstance) == 64);

    ParticlePass::ParticlePass(Device& device, const uint32_t frame_slots)
        : m_device(device), m_gpu(std::make_unique<GpuParticleSimulation>(device, frame_slots)),
          m_buffers(frame_slots), m_depth_bindings(frame_slots) {}
    ParticlePass::~ParticlePass() = default;
    void ParticlePass::reset() {
        clear_draws();
        m_gpu->reset();
        m_bindings.clear();
        for(auto& binding : m_depth_bindings)
            binding.reset();
    }
    Result<void, GraphicsError> ParticlePass::prepare_gpu(
        FrameScheduler& frames, const ViewProjectMatrix& camera, const RenderWorld& world) {
        return m_gpu->prepare(frames, camera,
            world.particles ? std::span(world.particles->gpu_emitters)
                            : std::span<const GpuParticleEmitter>{});
    }

    Result<std::unique_ptr<ParticlePass>, GraphicsError> ParticlePass::create(Device& device,
        PipelineManager& pipelines, RenderResources& resources, const uint32_t frame_slots,
        const SampleCount samples) {
        using Creation = Result<std::unique_ptr<ParticlePass>, GraphicsError>;
        static_assert(sizeof(BillboardCamera) == 96);
        if(frame_slots == 0)
            return Creation::failure({"Particle pass requires frame slots"});
        auto vertex = Shader::create(device, "billboard_vert", BILLBOARD_VERT);
        auto fragment = Shader::create(device, "billboard_frag", BILLBOARD_FRAG);
        if(!vertex)
            return Creation::failure(vertex.error());
        if(!fragment)
            return Creation::failure(fragment.error());
        ShaderLayout layout;
        DescriptorSetLayoutBindings bindings;
        bindings.add_binding(
            0, DescriptorType::CombinedImageSampler, Flags<ShaderStage>(ShaderStage::Fragment));
        auto image_layout = DescriptorSetLayout::create(device, bindings);
        if(!image_layout)
            return Creation::failure(image_layout.error());
        bindings.add_binding(
            1, DescriptorType::CombinedImageSampler, Flags<ShaderStage>(ShaderStage::Fragment));
        auto depth_layout = DescriptorSetLayout::create(device, bindings);
        if(!depth_layout)
            return Creation::failure(depth_layout.error());
        layout.descriptor_set_layouts = {image_layout.value(), depth_layout.value()};
        layout.push_constants.push_back(
            std::make_shared<PushConstantRange>(ShaderStage::Vertex, 0, sizeof(BillboardCamera)));
        layout.push_constants.push_back(
            std::make_shared<PushConstantRange>(ShaderStage::Fragment, 96, sizeof(Math::Vec4) * 2));
        VertexInputDescription input;
        input.add_binding(0, sizeof(ParticleInstance), VertexInputRate::Instance);
        input.add_attribute(
            0, 0, Format::R32G32B32A32_SFLOAT, offsetof(ParticleInstance, position_size));
        input.add_attribute(1, 0, Format::R32G32B32A32_SFLOAT, offsetof(ParticleInstance, color));
        input.add_attribute(2, 0, Format::R32G32B32A32_SFLOAT, offsetof(ParticleInstance, uv_rect));
        input.add_attribute(
            3, 0, Format::R32G32B32A32_SFLOAT, offsetof(ParticleInstance, end_position_size));
        PipelineConfig config;
        config.set_vertex_input_state(input);
        config.set_multisample_state(samples, false);
        config.set_depth_stencil_state({.depth_test_enable = true,
            .depth_write_enable = false,
            .depth_compare_op = CompareOp::LessEqual});
        config.set_dynamic_state({DynamicState::Viewport, DynamicState::Scissor});
        auto pass = std::unique_ptr<ParticlePass>(new ParticlePass(device, frame_slots));
        for(const auto blend : {ParticleBlend::Additive, ParticleBlend::Alpha}) {
            const bool alpha = blend == ParticleBlend::Alpha;
            config.set_color_blend_attachment_state({.blend_enable = true,
                .src_color_blend_factor = BlendFactor::SrcAlpha,
                .dst_color_blend_factor = alpha ? BlendFactor::OneMinusSrcAlpha : BlendFactor::One,
                .src_alpha_blend_factor = BlendFactor::One,
                .dst_alpha_blend_factor = BlendFactor::One,
                .alpha_blend_op = BlendOp::Max});
            auto pipeline =
                pipelines.create_pipeline(alpha ? "particle_alpha" : "particle_additive", layout,
                    config, vertex.value(), fragment.value());
            if(!pipeline)
                return Creation::failure(pipeline.error());
            pass->m_pipelines[static_cast<size_t>(blend)] = std::move(pipeline).value();
        }
        auto white = resources.try_create_texture(
            TextureData{.width = 1, .height = 1, .pixels = {255, 255, 255, 255}});
        if(!white)
            return Creation::failure(white.error());
        auto sampler =
            Sampler::create(device, {.address_mode_u = SamplerAddressMode::ClampToEdge,
                                        .address_mode_v = SamplerAddressMode::ClampToEdge,
                                        .address_mode_w = SamplerAddressMode::ClampToEdge});
        if(!sampler)
            return Creation::failure(sampler.error());
        auto depth_sampler =
            Sampler::create(device, {.mag_filter = Filter::Nearest,
                                        .min_filter = Filter::Nearest,
                                        .mipmap_mode = SamplerMipmapMode::Nearest,
                                        .address_mode_u = SamplerAddressMode::ClampToEdge,
                                        .address_mode_v = SamplerAddressMode::ClampToEdge});
        if(!depth_sampler)
            return Creation::failure(depth_sampler.error());
        pass->m_depth_sampler = std::move(depth_sampler).value();
        pass->m_depth_layout = std::move(depth_layout).value();
        pass->m_layout = std::move(image_layout).value();
        pass->m_white = std::move(white).value();
        pass->m_sampler = std::move(sampler).value();
        return Creation::success(std::move(pass));
    }

    Result<void, GraphicsError> ParticlePass::prepare_instances(const ParticleSnapshot& particles,
        std::span<const ParticleSnapshot::Batch> batches, const Math::Mat4& view, bool ordered) {
        using Prepared = Result<void, GraphicsError>;
        uint32_t covered = 0;
        for(const auto& batch : batches) {
            if(batch.first != covered || batch.count > particles.instances.size() - covered)
                return Prepared::failure({"Invalid particle batch range"});
            if(batch.blend != ParticleBlend::Additive && batch.blend != ParticleBlend::Alpha)
                return Prepared::failure({"Invalid particle blend mode"});
            if(!std::isfinite(batch.soft_distance) || batch.soft_distance < 0
                || batch.soft_distance > 1000)
                return Prepared::failure({"Invalid particle soft distance"});
            covered += batch.count;
            ordered |= batch.blend == ParticleBlend::Alpha && batch.count != 0;
        }
        if(covered != particles.instances.size())
            return Prepared::failure({"Particle batches do not cover the snapshot"});
        ordered |= std::ranges::any_of(particles.gpu_emitters,
            [](const auto& emitter) { return emitter.settings.blend == ParticleBlend::Alpha; });
        if(!ordered) {
            for(uint32_t source = 0; source < batches.size(); ++source) {
                const auto& batch = batches[source];
                if(batch.count)
                    m_draw_batches.push_back({batch.first, batch.count, source, batch.first});
            }
            for(uint32_t i = 0; i < particles.gpu_emitters.size(); ++i)
                m_draw_batches.push_back({covered + i, 1, uint32_t(batches.size()) + i, 0, true});
            return Prepared::success();
        }
        // Alpha 会衰减后方火花，因此混合场景中的加法粒子也参与排序。
        // 只重排 Render 私有上传数据，Main 快照可继续被其他相机使用。
        m_order.reserve(covered);
        for(uint32_t source = 0; source < batches.size(); ++source) {
            const auto& batch = batches[source];
            for(uint32_t index = batch.first; index < batch.first + batch.count; ++index) {
                const auto& instance = particles.instances[index];
                auto center = Math::Vec3(instance.position_size);
                if(instance.end_position_size.w >= 0)
                    center = (center + Math::Vec3(instance.end_position_size)) * 0.5f;
                const auto position = Math::Vec4(center, 1);
                m_order.push_back({index, source, DrawOrder::camera_depth(view, position)});
            }
        }
        for(uint32_t i = 0; i < particles.gpu_emitters.size(); ++i) {
            const auto& emitter = particles.gpu_emitters[i];
            m_order.push_back({i, uint32_t(batches.size()) + i,
                DrawOrder::camera_depth(view, Math::Vec4(emitter.position, 1)), true});
        }
        std::sort(m_order.begin(), m_order.end(), [](const auto& a, const auto& b) {
            if(a.depth != b.depth)
                return a.depth < b.depth;
            if(a.gpu != b.gpu)
                return a.gpu < b.gpu;
            return a.instance < b.instance;
        });
        m_sorted_instances.reserve(covered);
        m_depths.reserve(covered);
        for(const auto& entry : m_order) {
            if(entry.gpu) {
                m_draw_batches.push_back({uint32_t(m_depths.size()), 1, entry.batch, 0, true});
                m_depths.push_back(entry.depth);
                continue;
            }
            if(!m_draw_batches.empty() && !m_draw_batches.back().gpu
                && m_draw_batches.back().source == entry.batch)
                ++m_draw_batches.back().count;
            else
                m_draw_batches.push_back({uint32_t(m_depths.size()), 1, entry.batch,
                    uint32_t(m_sorted_instances.size())});
            m_sorted_instances.push_back(particles.instances[entry.instance]);
            m_depths.push_back(entry.depth);
        }
        return Prepared::success();
    }

    void ParticlePass::clear_draws() {
        m_order.clear();
        m_sorted_instances.clear();
        m_draw_batches.clear();
        m_sources.clear();
        m_depths.clear();
        m_instance_count = 0;
    }

    Result<std::vector<QueueSemaphoreSubmit>, GraphicsError> ParticlePass::prepare(
        FrameScheduler& frames, const ViewProjectMatrix& camera, const RenderWorld& world,
        const bool sort_for_meshes, std::shared_ptr<ImageView> scene_depth, const FogVolume* fog) {
        using Prepared = Result<std::vector<QueueSemaphoreSubmit>, GraphicsError>;
        clear_draws();
        if(!world.particles
            || (world.particles->instances.empty() && world.particles->gpu_emitters.empty())) {
            reset();
            return Prepared::success({});
        }
        const auto& particles = *world.particles;
        if(particles.gpu_emitters.empty())
            m_gpu->reset();
        const auto count = particles.instances.size();
        if(count > ParticleSnapshot::MAX_PARTICLES)
            return Prepared::failure({"Particle snapshot exceeds scene capacity"});
        const ParticleSnapshot::Batch untextured{{}, 0, uint32_t(count)};
        auto batches = std::span(particles.batches);
        const bool implicit_batch = batches.empty() && count;
        if(batches.empty() && count)
            batches = std::span(&untextured, 1);
        else if(world.particle_textures.size() != batches.size() + particles.gpu_emitters.size())
            return Prepared::failure({"Particle texture bindings do not match the snapshot"});
        if(auto prepared = prepare_instances(particles, batches, camera.view, sort_for_meshes);
            !prepared)
            return Prepared::failure(prepared.error());
        if(particles.uses_soft_depth() && !scene_depth)
            return Prepared::failure({"Soft particles require scene depth"});
        auto& depth_binding = m_depth_bindings.at(frames.get_current_frame_slot_index());
        if(!scene_depth)
            scene_depth = m_white->get_image_view();
        const auto fog_image = fog ? fog->image : m_white->get_image_view();
        const std::vector depth_images{scene_depth, fog_image};
        if(!depth_binding || depth_binding->images != depth_images) {
            auto binding = SampledImageBinding::create(
                m_device, depth_images, m_depth_layout, m_depth_sampler);
            if(!binding)
                return Prepared::failure(binding.error());
            depth_binding = std::move(binding).value();
        }
        frames.retain_current_frame_resource(depth_binding);
        if(particles.uses_soft_depth() || fog) {
            const auto inverse_projection = Math::inverse(camera.projection);
            if(!Math::is_finite(inverse_projection))
                return Prepared::failure({"Invalid particle projection"});
            m_depth_coefficients = {inverse_projection[2][2], inverse_projection[3][2],
                inverse_projection[2][3], inverse_projection[3][3]};
        }
        auto& buffer = m_buffers.at(frames.get_current_frame_slot_index());
        const auto bytes = count * sizeof(ParticleInstance);
        if(bytes && (!buffer || buffer->get_size() < bytes)) {
            auto allocated =
                Buffer::try_create_cpu_buffer(m_device, Flags<BufferUsage>(BufferUsage::Vertex),
                    std::bit_ceil(bytes), true, nullptr, "particle instances");
            if(!allocated)
                return Prepared::failure(allocated.error());
            buffer = std::move(allocated).value();
        }
        auto instances = std::span(particles.instances);
        if(!m_sorted_instances.empty())
            instances = m_sorted_instances;
        if(bytes) {
            buffer->write(instances.data(), bytes);
            frames.retain_current_frame_resource(buffer);
        }
        const auto pose = Math::inverse(camera.view);
        m_camera = {camera.projection * camera.view, pose[0], pose[1]};
        m_camera.right.w = fog ? fog->parameters.x : 0;
        m_camera.up.w = fog ? fog->parameters.y : 0;
        std::vector<QueueSemaphoreSubmit> waits;
        const auto& white_ready = m_white->get_ready_completion();
        if(white_ready.is_valid())
            merge_semaphore_wait(waits, QueueSemaphoreSubmit(white_ready,
                                            Flags<PipelineStage>(PipelineStage::FragmentShader)));
        std::unordered_set<AssetHandle> used;
        std::vector<ParticleSnapshot::Batch> sources(batches.begin(), batches.end());
        for(const auto& emitter : particles.gpu_emitters)
            sources.push_back({emitter.settings.texture, 0, emitter.count, emitter.settings.blend,
                emitter.settings.soft_distance});
        m_sources.reserve(sources.size());
        for(size_t index = 0; index < sources.size(); ++index) {
            const auto& batch = sources[index];
            auto& source = m_sources.emplace_back(
                DrawSource{nullptr, batch.blend, bool(batch.texture), batch.soft_distance});
            if(index >= batches.size()) {
                const auto outputs = m_gpu->outputs();
                const auto gpu = index - batches.size();
                if(gpu >= outputs.size())
                    return Prepared::failure({"GPU particles must be prepared before drawing"});
                source.instances = outputs[gpu].instances;
                source.indirect = outputs[gpu].indirect;
            }
            auto texture = m_white;
            if(batch.texture)
                texture = world.particle_textures.at(index - (implicit_batch ? 1 : 0));
            if(!texture || !batch.count)
                continue;
            if(texture->get_image_view()->get_image()->get_info().cubemap)
                return Prepared::failure({"Particle texture must be two-dimensional"});
            used.insert(batch.texture);
            auto sampler = texture->get_sampler();
            if(!sampler)
                sampler = m_sampler;
            auto& binding = m_bindings[batch.texture];
            if(!binding || binding->images.front() != texture->get_image_view()
                || binding->samplers[0] != sampler) {
                auto candidate = SampledImageBinding::create(
                    m_device, {texture->get_image_view()}, m_layout, sampler);
                if(!candidate)
                    return Prepared::failure(candidate.error());
                binding = std::move(candidate).value();
            }
            source.binding = binding;
            frames.retain_current_frame_resource(binding);
            frames.retain_current_frame_resource(m_pipelines[static_cast<size_t>(batch.blend)]);
            const auto& completion = texture->get_ready_completion();
            if(completion.is_valid())
                merge_semaphore_wait(
                    waits, QueueSemaphoreSubmit(
                               completion, Flags<PipelineStage>(PipelineStage::FragmentShader)));
        }
        std::erase_if(m_bindings, [&](const auto& entry) { return !used.contains(entry.first); });
        m_instance_count = uint32_t(count + particles.gpu_emitters.size());
        return Prepared::success(std::move(waits));
    }

    void ParticlePass::record(FrameScheduler& frames, const uint32_t first, const uint32_t count) {
        if(!count)
            return;
        const auto& command = frames.get_current_command_buffer();
        const Pipeline* active_pipeline = nullptr;
        const auto end = first + count;
        auto batch = std::lower_bound(m_draw_batches.begin(), m_draw_batches.end(), first,
            [](const auto& range, uint32_t start) { return range.first + range.count <= start; });
        for(; batch != m_draw_batches.end() && batch->first < end; ++batch) {
            const auto& source = m_sources[batch->source];
            if(!source.binding)
                continue;
            const auto& pipeline = m_pipelines[static_cast<size_t>(source.blend)];
            const auto& layout = *pipeline->get_layout();
            if(active_pipeline != pipeline.get()) {
                command.bind_pipeline(*pipeline);
                command.push_constants(layout, Flags<ShaderStage>(ShaderStage::Vertex), 0,
                    &m_camera, sizeof(m_camera));
                const auto& depth = m_depth_bindings.at(frames.get_current_frame_slot_index());
                command.bind_descriptor_sets(layout, std::span(&depth->descriptor, 1), 1);
                active_pipeline = pipeline.get();
            }
            command.bind_descriptor_sets(layout, std::span(&source.binding->descriptor, 1));
            const std::array<float, 8> settings{m_depth_coefficients.x, m_depth_coefficients.y,
                m_depth_coefficients.z, m_depth_coefficients.w, source.textured ? 1.0f : 0.0f,
                source.soft_distance, source.blend == ParticleBlend::Additive ? 1.0f : 0.0f, 0};
            command.push_constants(layout, Flags<ShaderStage>(ShaderStage::Fragment), 96,
                settings.data(), sizeof(settings));
            const auto begin = std::max(first, batch->first);
            if(batch->gpu) {
                command.bind_vertex_buffer({*source.instances, 0});
                command.get().drawIndirect(
                    source.indirect->get(), 0, 1, sizeof(vk::DrawIndirectCommand));
            } else {
                command.bind_vertex_buffer(
                    {*m_buffers.at(frames.get_current_frame_slot_index()), 0});
                command.draw(6, std::min(end, batch->first + batch->count) - begin, 0,
                    batch->buffer_first + begin - batch->first);
            }
        }
    }
}
