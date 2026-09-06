#include "render/debug/debug_renderer.h"

#include "diagnostics/logger.h"
#include "graphics/command/command_buffer.h"
#include "graphics/pipeline/pipeline.h"
#include "graphics/pipeline/shader.h"
#include "graphics/pipeline/vertex_description.h"
#include "graphics/resource/buffer.h"
#include "render/frame_scheduler.h"
#include "render/resource/resource_manager.h"

#include "debug_line_frag.h"
#include "debug_line_vert.h"

#include <limits>
#include <stdexcept>

namespace Comet {
    DebugRenderer::DebugRenderer(Device& device, PipelineManager& pipeline_manager,
        ResourceManager& resource_manager, const uint32_t frame_slot_count,
        const SampleCount sample_count)
        : m_device(device), m_frame_resources(frame_slot_count) {
        auto& shaders = resource_manager.get_shader_manager();
        m_pipeline = create_pipeline(pipeline_manager,
            shaders.load_shader_if_missing("debug_line_vert", DEBUG_LINE_VERT),
            shaders.load_shader_if_missing("debug_line_frag", DEBUG_LINE_FRAG),
            sample_count);
    }

    std::shared_ptr<Pipeline> DebugRenderer::create_pipeline(
        PipelineManager& pipeline_manager, const std::shared_ptr<Shader>& vertex_shader,
        const std::shared_ptr<Shader>& fragment_shader, const SampleCount sample_count) {
        ShaderLayout layout;
        layout.push_constants.push_back(std::make_shared<PushConstantRange>(
            ShaderStage::Vertex, 0, sizeof(Math::Mat4)));

        VertexInputDescription vertex_input;
        vertex_input.add_binding(
            0, sizeof(LineDrawList::Vertex), VertexInputRate::Vertex);
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
            "debug_line_pipeline", layout, config, vertex_shader, fragment_shader);
    }

    bool DebugRenderer::reload_shaders(PipelineManager& pipelines, ShaderManager& shaders,
        const ShaderManager::Bytecodes& bytecodes, const SampleCount samples) {
        if(bytecodes.size() != 2 || !bytecodes.contains("debug_line_vert")
            || !bytecodes.contains("debug_line_frag"))
            throw std::invalid_argument(
                "Debug Shader reload requires the complete two-Shader cohort");
        auto candidate_shaders = shaders.prepare_update(bytecodes);
        for(const auto* name : {"debug_line_vert", "debug_line_frag"}) {
            if(!shaders.get_shader(name)->get_interface().has_same_layout(
                   candidate_shaders.at(name)->get_interface()))
                throw std::invalid_argument(
                    std::string("Debug Shader changed a fixed interface: ") + name);
        }
        auto candidate =
            create_pipeline(pipelines, candidate_shaders.at("debug_line_vert"),
                candidate_shaders.at("debug_line_frag"), samples);
        const bool changed = candidate != m_pipeline;
        shaders.publish_update(candidate_shaders);
        m_pipeline.swap(candidate);
        return changed;
    }

    void DebugRenderer::render(FrameScheduler& frame_scheduler,
        const ViewProjectMatrix& view_project, const LineDrawList& draw_list) {
        const auto vertices = draw_list.vertices();
        if(vertices.empty()) {
            return;
        }
        if(vertices.size() > std::numeric_limits<uint32_t>::max()) {
            LOG_ERROR("Debug draw vertex count exceeds uint32_t range");
            return;
        }
        auto& resources =
            m_frame_resources.at(frame_scheduler.get_current_frame_slot_index());
        if(!ensure_capacity(resources, vertices.size())) {
            return;
        }
        resources.vertex_buffer->write(vertices.data(), vertices.size_bytes());
        frame_scheduler.retain_current_frame_resource(resources.vertex_buffer);
        frame_scheduler.retain_current_frame_resource(m_pipeline);

        const auto& command_buffer = frame_scheduler.get_current_command_buffer();
        const Math::Mat4 view_projection = view_project.projection * view_project.view;
        command_buffer.bind_pipeline(*m_pipeline);
        command_buffer.bind_vertex_buffer({*resources.vertex_buffer, 0});
        command_buffer.push_constants(*m_pipeline->get_layout(),
            Flags<ShaderStage>(ShaderStage::Vertex), 0, &view_projection,
            sizeof(view_projection));
        command_buffer.draw(static_cast<uint32_t>(vertices.size()));
    }

    bool DebugRenderer::ensure_capacity(
        FrameResources& resources, const std::size_t vertex_count) {
        if(vertex_count
            > std::numeric_limits<std::size_t>::max() / sizeof(LineDrawList::Vertex)) {
            LOG_ERROR("Debug draw vertex buffer size overflow");
            return false;
        }
        const std::size_t required_bytes = vertex_count * sizeof(LineDrawList::Vertex);
        std::size_t capacity = 256 * sizeof(LineDrawList::Vertex);
        if(resources.vertex_buffer) {
            capacity = resources.vertex_buffer->get_size();
            if(capacity >= required_bytes) {
                return true;
            }
        }
        if(resources.growth_retry_requests > 0) {
            --resources.growth_retry_requests;
            return false;
        }
        while(capacity < required_bytes) {
            if(capacity > std::numeric_limits<std::size_t>::max() / 2) {
                capacity = required_bytes;
                break;
            }
            capacity *= 2;
        }
        auto candidate = Buffer::try_create_cpu_buffer(m_device,
            Flags<BufferUsage>(BufferUsage::Vertex), capacity, true, nullptr,
            "debug line vertex buffer");
        if(!candidate) {
            LOG_ERROR("Failed to grow debug line vertex buffer to {} bytes: {}", capacity,
                vk::to_string(candidate.result()));
            // 调试绘制非关键；保留旧 buffer，跳过本批，避免每次请求都重试并刷日志。
            resources.growth_retry_requests = 120;
            return false;
        }
        resources.vertex_buffer = std::move(candidate).value();
        return true;
    }
}
