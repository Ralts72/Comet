#include "render/passes/particle_pass.h"

#include "graphics/command/command_buffer.h"
#include "graphics/pipeline/pipeline.h"
#include "graphics/pipeline/shader.h"
#include "graphics/pipeline/vertex_description.h"
#include "graphics/resource/buffer.h"
#include "render/frame_scheduler.h"

#include "billboard_frag.h"
#include "billboard_vert.h"

#include <bit>
#include <cstddef>
#include <utility>

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

    ParticlePass::ParticlePass(
        Device& device, std::shared_ptr<Pipeline> pipeline, const uint32_t frame_slots)
        : m_device(device), m_pipeline(std::move(pipeline)), m_buffers(frame_slots) {}

    Result<std::unique_ptr<ParticlePass>, GraphicsError> ParticlePass::create(Device& device,
        PipelineManager& pipelines, const uint32_t frame_slots, const SampleCount samples) {
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
        layout.push_constants.push_back(
            std::make_shared<PushConstantRange>(ShaderStage::Vertex, 0, sizeof(BillboardCamera)));
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
        config.set_color_blend_attachment_state({.blend_enable = true,
            .src_color_blend_factor = BlendFactor::SrcAlpha,
            .dst_color_blend_factor = BlendFactor::One,
            .src_alpha_blend_factor = BlendFactor::Zero,
            .dst_alpha_blend_factor = BlendFactor::One});
        config.set_dynamic_state({DynamicState::Viewport, DynamicState::Scissor});
        auto pipeline = pipelines.create_pipeline(
            "particle_billboards", layout, config, vertex.value(), fragment.value());
        if(!pipeline)
            return Creation::failure(pipeline.error());
        return Creation::success(std::unique_ptr<ParticlePass>(
            new ParticlePass(device, std::move(pipeline).value(), frame_slots)));
    }

    Result<void, GraphicsError> ParticlePass::render(FrameScheduler& frames,
        const ViewProjectMatrix& camera, const ParticleSnapshot& particles) {
        const auto count = particles.instances.size();
        if(count == 0)
            return Result<void, GraphicsError>::success();
        if(count > ParticleSnapshot::MAX_PARTICLES)
            return Result<void, GraphicsError>::failure(
                {"Particle snapshot exceeds scene capacity"});
        auto& buffer = m_buffers.at(frames.get_current_frame_slot_index());
        const auto bytes = count * sizeof(ParticleInstance);
        if(!buffer || buffer->get_size() < bytes) {
            auto allocated =
                Buffer::try_create_cpu_buffer(m_device, Flags<BufferUsage>(BufferUsage::Vertex),
                    std::bit_ceil(bytes), true, nullptr, "particle instances");
            if(!allocated)
                return Result<void, GraphicsError>::failure(allocated.error());
            buffer = std::move(allocated).value();
        }
        buffer->write(particles.instances.data(), bytes);
        frames.retain_current_frame_resource(buffer);
        frames.retain_current_frame_resource(m_pipeline);
        const auto pose = Math::inverse(camera.view);
        const BillboardCamera constants{camera.projection * camera.view, pose[0], pose[1]};
        const auto& command = frames.get_current_command_buffer();
        command.bind_pipeline(*m_pipeline);
        command.bind_vertex_buffer({*buffer, 0});
        command.push_constants(*m_pipeline->get_layout(), Flags<ShaderStage>(ShaderStage::Vertex),
            0, &constants, sizeof(constants));
        command.draw(6, uint32_t(count));
        return Result<void, GraphicsError>::success();
    }
}
