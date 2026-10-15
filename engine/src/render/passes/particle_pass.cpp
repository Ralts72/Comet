#include "render/passes/particle_pass.h"

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

#include "billboard_frag.h"
#include "billboard_vert.h"

#include <bit>
#include <cstddef>
#include <utility>
#include <unordered_set>
#include <algorithm>
#include <cmath>

namespace Comet {
    namespace {
        struct BillboardCamera {
            Math::Mat4 view_projection;
            Math::Vec4 right;
            Math::Vec4 up;
        };
        static_assert(sizeof(BillboardCamera) == 96);
        static_assert(sizeof(ParticleInstance) == 32);
    }

    ParticlePass::ParticlePass(Device& device, const uint32_t frame_slots)
        : m_device(device), m_buffers(frame_slots) {}

    Result<std::unique_ptr<ParticlePass>, GraphicsError> ParticlePass::create(Device& device,
        PipelineManager& pipelines, RenderResources& resources, const uint32_t frame_slots,
        const SampleCount samples) {
        using Creation = Result<std::unique_ptr<ParticlePass>, GraphicsError>;
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
        layout.descriptor_set_layouts = {image_layout.value()};
        layout.push_constants.push_back(
            std::make_shared<PushConstantRange>(ShaderStage::Vertex, 0, sizeof(BillboardCamera)));
        layout.push_constants.push_back(
            std::make_shared<PushConstantRange>(ShaderStage::Fragment, 96, sizeof(uint32_t)));
        VertexInputDescription input;
        input.add_binding(0, sizeof(ParticleInstance), VertexInputRate::Instance);
        input.add_attribute(
            0, 0, Format::R32G32B32A32_SFLOAT, offsetof(ParticleInstance, position_size));
        input.add_attribute(1, 0, Format::R32G32B32A32_SFLOAT, offsetof(ParticleInstance, color));
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
                .src_alpha_blend_factor = BlendFactor::Zero,
                .dst_alpha_blend_factor = BlendFactor::One});
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
        pass->m_layout = std::move(image_layout).value();
        pass->m_white = std::move(white).value();
        pass->m_sampler = std::move(sampler).value();
        return Creation::success(std::move(pass));
    }

    Result<void, GraphicsError> ParticlePass::prepare_instances(const ParticleSnapshot& particles,
        std::span<const ParticleSnapshot::Batch> batches, const Math::Mat4& view) {
        using Prepared = Result<void, GraphicsError>;
        m_order.clear();
        m_sorted_instances.clear();
        m_draw_batches.clear();
        uint32_t covered = 0;
        bool ordered = false;
        for(const auto& batch : batches) {
            if(batch.first != covered || batch.count > particles.instances.size() - covered)
                return Prepared::failure({"Invalid particle batch range"});
            if(batch.blend != ParticleBlend::Additive && batch.blend != ParticleBlend::Alpha)
                return Prepared::failure({"Invalid particle blend mode"});
            covered += batch.count;
            ordered |= batch.blend == ParticleBlend::Alpha && batch.count != 0;
        }
        if(covered != particles.instances.size())
            return Prepared::failure({"Particle batches do not cover the snapshot"});
        if(!ordered) {
            for(uint32_t source = 0; source < batches.size(); ++source) {
                const auto& batch = batches[source];
                if(batch.count)
                    m_draw_batches.push_back({batch.first, batch.count, source});
            }
            return Prepared::success();
        }
        // Alpha 会衰减后方火花，因此混合场景中的加法粒子也参与排序。
        // 只重排 Render 私有上传数据，Main 快照可继续被其他相机使用。
        m_order.reserve(covered);
        for(uint32_t source = 0; source < batches.size(); ++source) {
            const auto& batch = batches[source];
            for(uint32_t index = batch.first; index < batch.first + batch.count; ++index) {
                const auto position =
                    Math::Vec4(Math::Vec3(particles.instances[index].position_size), 1);
                const float depth = (view * position).z;
                m_order.push_back({index, source, std::isfinite(depth) ? depth : 0});
            }
        }
        std::sort(m_order.begin(), m_order.end(), [](const auto& a, const auto& b) {
            if(a.depth != b.depth)
                return a.depth < b.depth;
            return a.instance < b.instance;
        });
        m_sorted_instances.reserve(covered);
        for(const auto& entry : m_order) {
            if(!m_draw_batches.empty() && m_draw_batches.back().source == entry.batch)
                ++m_draw_batches.back().count;
            else
                m_draw_batches.push_back({uint32_t(m_sorted_instances.size()), 1, entry.batch});
            m_sorted_instances.push_back(particles.instances[entry.instance]);
        }
        return Prepared::success();
    }

    Result<std::vector<QueueSemaphoreSubmit>, GraphicsError> ParticlePass::render(
        FrameScheduler& frames, const ViewProjectMatrix& camera, const RenderWorld& world) {
        using Draw = Result<std::vector<QueueSemaphoreSubmit>, GraphicsError>;
        if(!world.particles) {
            reset();
            return Draw::success({});
        }
        const auto& particles = *world.particles;
        const auto count = particles.instances.size();
        if(count == 0) {
            m_bindings.clear();
            return Draw::success({});
        }
        if(count > ParticleSnapshot::MAX_PARTICLES)
            return Draw::failure({"Particle snapshot exceeds scene capacity"});
        const ParticleSnapshot::Batch untextured{{}, 0, uint32_t(count)};
        auto batches = std::span(particles.batches);
        if(batches.empty())
            batches = std::span(&untextured, 1);
        else if(world.particle_textures.size() != batches.size())
            return Draw::failure({"Particle texture bindings do not match the snapshot"});
        if(auto prepared = prepare_instances(particles, batches, camera.view); !prepared)
            return Draw::failure(prepared.error());
        auto& buffer = m_buffers.at(frames.get_current_frame_slot_index());
        const auto bytes = count * sizeof(ParticleInstance);
        if(!buffer || buffer->get_size() < bytes) {
            auto allocated =
                Buffer::try_create_cpu_buffer(m_device, Flags<BufferUsage>(BufferUsage::Vertex),
                    std::bit_ceil(bytes), true, nullptr, "particle instances");
            if(!allocated)
                return Draw::failure(allocated.error());
            buffer = std::move(allocated).value();
        }
        auto instances = std::span(particles.instances);
        if(!m_sorted_instances.empty())
            instances = m_sorted_instances;
        buffer->write(instances.data(), bytes);
        frames.retain_current_frame_resource(buffer);
        const auto pose = Math::inverse(camera.view);
        const BillboardCamera constants{camera.projection * camera.view, pose[0], pose[1]};
        const auto& command = frames.get_current_command_buffer();
        command.bind_vertex_buffer({*buffer, 0});
        const auto& layout = *m_pipelines.front()->get_layout();
        command.push_constants(
            layout, Flags<ShaderStage>(ShaderStage::Vertex), 0, &constants, sizeof(constants));
        std::vector<QueueSemaphoreSubmit> waits;
        std::unordered_set<AssetHandle> used;
        const auto draw = [&](const ParticleSnapshot::Batch& batch, const DrawBatch& range,
                              std::shared_ptr<Texture> texture) -> Result<void, GraphicsError> {
            if(batch.texture && !texture)
                return Result<void, GraphicsError>::success();
            if(!batch.texture)
                texture = m_white;
            if(texture->get_image_view()->get_image()->get_info().cubemap)
                return Result<void, GraphicsError>::failure(
                    {"Particle texture must be two-dimensional"});
            used.insert(batch.texture);
            auto sampler = texture->get_sampler();
            if(!sampler)
                sampler = m_sampler;
            auto& binding = m_bindings[batch.texture];
            if(!binding || binding->images.front() != texture->get_image_view()
                || binding->sampler != sampler) {
                auto candidate = SampledImageBinding::create(
                    m_device, {texture->get_image_view()}, m_layout, sampler);
                if(!candidate)
                    return Result<void, GraphicsError>::failure(candidate.error());
                binding = std::move(candidate).value();
            }
            frames.retain_current_frame_resource(binding);
            const auto& pipeline = m_pipelines[static_cast<size_t>(batch.blend)];
            frames.retain_current_frame_resource(pipeline);
            command.bind_pipeline(*pipeline);
            command.bind_descriptor_sets(layout, std::span(&binding->descriptor, 1));
            const uint32_t textured = batch.texture ? 1 : 0;
            command.push_constants(
                layout, Flags<ShaderStage>(ShaderStage::Fragment), 96, &textured, sizeof(textured));
            command.draw(6, range.count, 0, range.first);
            const auto& completion = texture->get_ready_completion();
            if(completion.is_valid())
                merge_semaphore_wait(
                    waits, QueueSemaphoreSubmit(
                               completion, Flags<PipelineStage>(PipelineStage::FragmentShader)));
            return Result<void, GraphicsError>::success();
        };
        for(const auto& range : m_draw_batches) {
            std::shared_ptr<Texture> texture;
            if(!particles.batches.empty())
                texture = world.particle_textures[range.source];
            if(auto result = draw(batches[range.source], range, std::move(texture)); !result)
                return Draw::failure(result.error());
        }
        std::erase_if(m_bindings, [&](const auto& entry) { return !used.contains(entry.first); });
        return Draw::success(std::move(waits));
    }
}
