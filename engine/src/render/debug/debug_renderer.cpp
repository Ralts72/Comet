#include "render/debug/debug_renderer.h"

#include "diagnostics/logger.h"
#include "graphics/command/command_buffer.h"
#include "graphics/pipeline/pipeline.h"
#include "graphics/pipeline/shader.h"
#include "graphics/pipeline/vertex_description.h"
#include "graphics/resource/buffer.h"
#include "render/frame_scheduler.h"
#include "render/resource/irradiance.h"
#include "render/scene/render_submission.h"

#include "line_frag.h"
#include "line_vert.h"
#include "irradiance_debug_vert.h"
#include "irradiance_debug_frag.h"

#include <array>
#include <cstddef>
#include <limits>
#include <utility>

namespace Comet {
    namespace {
        struct ProbeDrawParameters {
            Math::Vec4 origin_radius;
            Math::Vec4 spacing;
            Math::Vec4 dimensions_pixel;
            std::array<uint32_t, 4> flags;
        };
        constexpr auto PROBE_FRAGMENT_OFFSET = offsetof(ProbeDrawParameters, flags);
        constexpr auto PROBE_FRAGMENT_SIZE = sizeof(ProbeDrawParameters::flags);
        static_assert(PROBE_FRAGMENT_OFFSET == 48 && sizeof(ProbeDrawParameters) == 64);
    }

    DebugRenderer::DebugRenderer(Device& device, PipelineManager& pipelines,
        std::shared_ptr<Pipeline> pipeline, const uint32_t frame_slot_count, SampleCount samples,
        std::shared_ptr<DescriptorSetLayout> view_layout)
        : m_device(device), m_pipelines(pipelines), m_samples(samples),
          m_pipeline(std::move(pipeline)), m_view_layout(std::move(view_layout)),
          m_frame_resources(frame_slot_count) {}

    Result<std::unique_ptr<DebugRenderer>, GraphicsError> DebugRenderer::create(Device& device,
        PipelineManager& pipeline_manager, const uint32_t frame_slot_count,
        const SampleCount sample_count, const std::shared_ptr<DescriptorSetLayout>& view_layout) {
        using Creation = Result<std::unique_ptr<DebugRenderer>, GraphicsError>;
        if(frame_slot_count == 0)
            return Creation::failure({"Debug renderer requires frame slots"});
        auto pipeline = create_pipeline(device, pipeline_manager, sample_count);
        if(!pipeline)
            return Creation::failure(pipeline.error());
        return Creation::success(
            std::unique_ptr<DebugRenderer>(new DebugRenderer(device, pipeline_manager,
                std::move(pipeline).value(), frame_slot_count, sample_count, view_layout)));
    }

    Result<std::shared_ptr<Pipeline>, GraphicsError> DebugRenderer::create_pipeline(
        Device& device, PipelineManager& pipeline_manager, const SampleCount sample_count) {
        using Creation = Result<std::shared_ptr<Pipeline>, GraphicsError>;
        const auto vertex_shader = Shader::create(device, "line_vert", LINE_VERT);
        if(!vertex_shader)
            return Creation::failure(vertex_shader.error());
        const auto fragment_shader = Shader::create(device, "line_frag", LINE_FRAG);
        if(!fragment_shader)
            return Creation::failure(fragment_shader.error());
        ShaderLayout layout;
        layout.push_constants.push_back(
            std::make_shared<PushConstantRange>(ShaderStage::Vertex, 0, sizeof(Math::Mat4)));

        VertexInputDescription vertex_input;
        vertex_input.add_binding(0, sizeof(LineDrawList::Vertex), VertexInputRate::Vertex);
        vertex_input.add_attribute(
            0, 0, Format::R32G32B32_SFLOAT, offsetof(LineDrawList::Vertex, position));
        vertex_input.add_attribute(
            1, 0, Format::R32G32B32A32_SFLOAT, offsetof(LineDrawList::Vertex, color));

        PipelineConfig config;
        config.set_vertex_input_state(vertex_input);
        config.set_input_assembly_state(Topology::LineList);
        config.set_multisample_state(sample_count, false);
        config.set_depth_stencil_state({
            .depth_test_enable = true,
            .depth_write_enable = false,
            .depth_compare_op = CompareOp::LessEqual,
        });
        config.enable_alpha_blend();
        config.set_dynamic_state({DynamicState::Viewport, DynamicState::Scissor});

        return pipeline_manager.create_pipeline(
            "debug_line_pipeline", layout, config, vertex_shader.value(), fragment_shader.value());
    }

    Result<void, GraphicsError> DebugRenderer::render(FrameScheduler& frame_scheduler,
        const ViewProjectMatrix& view_project, const LineDrawList& draw_list) {
        const auto vertices = draw_list.vertices();
        if(vertices.empty()) {
            return Result<void, GraphicsError>::success();
        }
        if(vertices.size() > std::numeric_limits<uint32_t>::max()) {
            LOG_ERROR("Debug draw vertex count exceeds uint32_t range");
            return Result<void, GraphicsError>::success();
        }
        auto& resources = m_frame_resources.at(frame_scheduler.get_current_frame_slot_index());
        const auto capacity = ensure_capacity(resources, vertices.size());
        if(!capacity)
            return Result<void, GraphicsError>::failure(capacity.error());
        if(!capacity.value())
            return Result<void, GraphicsError>::success();
        resources.vertex_buffer->write(vertices.data(), vertices.size_bytes());
        frame_scheduler.retain_current_frame_resource(resources.vertex_buffer);
        frame_scheduler.retain_current_frame_resource(m_pipeline);

        const auto& command_buffer = frame_scheduler.get_current_command_buffer();
        const Math::Mat4 view_projection = view_project.projection * view_project.view;
        command_buffer.bind_pipeline(*m_pipeline);
        command_buffer.bind_vertex_buffer({*resources.vertex_buffer, 0});
        command_buffer.push_constants(*m_pipeline->get_layout(),
            Flags<ShaderStage>(ShaderStage::Vertex), 0, &view_projection, sizeof(view_projection));
        command_buffer.draw(static_cast<uint32_t>(vertices.size()));
        return Result<void, GraphicsError>::success();
    }

    Result<void, GraphicsError> DebugRenderer::prepare_irradiance_pipeline() {
        if(m_irradiance_pipeline)
            return Result<void, GraphicsError>::success();
        if(!m_view_layout)
            return Result<void, GraphicsError>::failure({"Probe debug requires view bindings"});
        auto vertex = Shader::create(m_device, "probe markers", IRRADIANCE_DEBUG_VERT);
        auto fragment = Shader::create(m_device, "probe status", IRRADIANCE_DEBUG_FRAG);
        if(!vertex)
            return Result<void, GraphicsError>::failure(vertex.error());
        if(!fragment)
            return Result<void, GraphicsError>::failure(fragment.error());
        ShaderLayout layout;
        layout.descriptor_set_layouts = {m_view_layout};
        layout.push_constants = {
            std::make_shared<PushConstantRange>(ShaderStage::Vertex, 0, PROBE_FRAGMENT_OFFSET),
            std::make_shared<PushConstantRange>(
                ShaderStage::Fragment, PROBE_FRAGMENT_OFFSET, PROBE_FRAGMENT_SIZE)};
        PipelineConfig config;
        config.set_multisample_state(m_samples, false);
        config.set_dynamic_state({DynamicState::Viewport, DynamicState::Scissor});
        // 标记穿透遮挡，便于看到实心几何内部被停用的探针；不写入场景深度。
        config.set_depth_stencil_state({.depth_test_enable = false, .depth_write_enable = false});
        auto pipeline = m_pipelines.create_pipeline(
            "irradiance debug", layout, config, vertex.value(), fragment.value());
        if(!pipeline)
            return Result<void, GraphicsError>::failure(pipeline.error());
        m_irradiance_pipeline = std::move(pipeline).value();
        return Result<void, GraphicsError>::success();
    }

    Result<void, GraphicsError> DebugRenderer::render_irradiance(FrameScheduler& frames,
        const DescriptorSet& view, const std::shared_ptr<Irradiance>& irradiance,
        Math::Vec2u target_size, IrradianceDebugMode mode,
        const ResolvedIrradianceVolume* realtime) {
        if(mode == IrradianceDebugMode::Off || !irradiance)
            return Result<void, GraphicsError>::success();
        if(auto prepared = prepare_irradiance_pipeline(); !prepared)
            return prepared;
        const auto count =
            irradiance->dimensions.x * irradiance->dimensions.y * irradiance->dimensions.z;
        // 只使用实际绑定资源的更新状态；烘焙探针已经全部求值。
        const bool live = realtime && realtime->resource == irradiance;
        const auto evaluated = live ? realtime->evaluated_probes : count;
        const ProbeDrawParameters parameters{Math::Vec4(irradiance->origin, 4),
            Math::Vec4(irradiance->spacing, 0),
            {float(irradiance->dimensions.x), float(irradiance->dimensions.y), 1.f / target_size.x,
                1.f / target_size.y},
            {live ? realtime->updated_first : 0, live ? realtime->updated_count : 0, count,
                uint32_t(mode) | (evaluated << 2)}};
        const auto& command = frames.get_current_command_buffer();
        frames.retain_current_frame_resource(m_irradiance_pipeline);
        command.bind_pipeline(*m_irradiance_pipeline);
        command.bind_descriptor_sets(*m_irradiance_pipeline->get_layout(), std::span(&view, 1));
        command.push_constants(*m_irradiance_pipeline->get_layout(),
            Flags<ShaderStage>(ShaderStage::Vertex), 0, &parameters, PROBE_FRAGMENT_OFFSET);
        command.push_constants(*m_irradiance_pipeline->get_layout(),
            Flags<ShaderStage>(ShaderStage::Fragment), PROBE_FRAGMENT_OFFSET,
            parameters.flags.data(), PROBE_FRAGMENT_SIZE);
        command.draw(6, count);
        return Result<void, GraphicsError>::success();
    }

    Result<bool, GraphicsError> DebugRenderer::ensure_capacity(
        FrameResources& resources, const std::size_t vertex_count) {
        if(vertex_count > std::numeric_limits<std::size_t>::max() / sizeof(LineDrawList::Vertex)) {
            LOG_ERROR("Debug draw vertex buffer size overflow");
            return Result<bool, GraphicsError>::success(false);
        }
        const std::size_t required_bytes = vertex_count * sizeof(LineDrawList::Vertex);
        std::size_t capacity = 256 * sizeof(LineDrawList::Vertex);
        if(resources.vertex_buffer) {
            capacity = resources.vertex_buffer->get_size();
            if(capacity >= required_bytes) {
                return Result<bool, GraphicsError>::success(true);
            }
        }
        if(resources.growth_retry_requests > 0) {
            --resources.growth_retry_requests;
            return Result<bool, GraphicsError>::success(false);
        }
        while(capacity < required_bytes) {
            if(capacity > std::numeric_limits<std::size_t>::max() / 2) {
                capacity = required_bytes;
                break;
            }
            capacity *= 2;
        }
        auto candidate =
            Buffer::try_create_cpu_buffer(m_device, Flags<BufferUsage>(BufferUsage::Vertex),
                capacity, true, nullptr, "debug line vertex buffer");
        if(!candidate) {
            if(candidate.error().is_device_lost())
                return Result<bool, GraphicsError>::failure(candidate.error());
            LOG_ERROR("Failed to grow debug line vertex buffer to {} bytes: {}", capacity,
                candidate.error().message);
            // 调试绘制非关键；保留旧 buffer，跳过本批，避免每次请求都重试并刷日志。
            resources.growth_retry_requests = 120;
            return Result<bool, GraphicsError>::success(false);
        }
        resources.vertex_buffer = std::move(candidate).value();
        return Result<bool, GraphicsError>::success(true);
    }
}
