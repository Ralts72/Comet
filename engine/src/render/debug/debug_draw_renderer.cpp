#include "render/debug/debug_draw_renderer.h"

#include "diagnostics/logger.h"
#include "graphics/command/command_buffer.h"
#include "graphics/device.h"
#include "graphics/pipeline/pipeline.h"
#include "graphics/pipeline/shader.h"
#include "graphics/pipeline/vertex_description.h"
#include "graphics/resource/buffer.h"
#include "render/resource/resource_manager.h"

#include "debug_line_frag.h"
#include "debug_line_vert.h"

#include <algorithm>
#include <cstddef>
#include <limits>

namespace Comet {
    namespace {
        constexpr std::size_t INITIAL_VERTEX_CAPACITY = 256;
    }

    DebugDrawRenderer::DebugDrawRenderer(
        Device& device,
        PipelineManager& pipeline_manager,
        ResourceManager& resource_manager,
        const std::uint32_t frame_slot_count,
        const SampleCount sample_count)
        : m_device(device),
          m_frame_resources(frame_slot_count) {
        if(frame_slot_count == 0) {
            LOG_FATAL("DebugDrawRenderer requires at least one frame slot");
        }

        ShaderLayout layout;
        layout.push_constants.push_back(std::make_shared<PushConstantRange>(
            ShaderStage::Vertex,
            0,
            sizeof(Math::Mat4)));

        VertexInputDescription vertex_input;
        vertex_input.add_binding(
            0, sizeof(DebugLineVertex), VertexInputRate::Vertex);
        vertex_input.add_attribute(
            0,
            0,
            Format::R32G32B32_SFLOAT,
            offsetof(DebugLineVertex, position));
        vertex_input.add_attribute(
            1,
            0,
            Format::R32G32B32A32_SFLOAT,
            offsetof(DebugLineVertex, color));

        PipelineConfig config;
        config.set_vertex_input_state(vertex_input);
        config.set_input_assembly_state(Topology::LineList);
        config.set_multisample_state(sample_count, false);
        config.set_depth_stencil_state({
            .depth_test_enable = true,
            .depth_write_enable = false,
            .depth_compare_op = CompareOp::LessEqual
        });
        config.enable_alpha_blend();
        config.set_dynamic_state({
            DynamicState::Viewport,
            DynamicState::Scissor
        });

        const auto vertex_shader = resource_manager.get_shader_manager()
            .load_shader("debug_line_vert", DEBUG_LINE_VERT);
        const auto fragment_shader = resource_manager.get_shader_manager()
            .load_shader("debug_line_frag", DEBUG_LINE_FRAG);
        m_pipeline = pipeline_manager.create_pipeline(
            "debug_line_pipeline",
            layout,
            config,
            vertex_shader,
            fragment_shader);
    }

    void DebugDrawRenderer::render(
        const CommandBuffer& command_buffer,
        const std::uint32_t frame_slot,
        const ViewProjectMatrix& view_project,
        const DebugDrawList& draw_list) {
        const std::span<const DebugLineVertex> vertices =
            draw_list.vertices();
        if(vertices.empty()) {
            return;
        }
        if(vertices.size() > std::numeric_limits<std::uint32_t>::max()) {
            LOG_ERROR("Debug draw vertex count exceeds uint32_t range");
            return;
        }

        FrameResources* resources = ensure_capacity(
            frame_slot, vertices.size());
        if(resources == nullptr) {
            return;
        }
        resources->vertex_buffer->write(
            vertices.data(),
            vertices.size_bytes());

        const Math::Mat4 view_projection =
            view_project.projection * view_project.view;
        command_buffer.bind_pipeline(*m_pipeline);
        command_buffer.bind_vertex_buffer({*resources->vertex_buffer, 0});
        command_buffer.push_constants(
            *m_pipeline->get_layout(),
            Flags<ShaderStage>(ShaderStage::Vertex),
            0,
            &view_projection,
            sizeof(view_projection));
        command_buffer.draw(static_cast<std::uint32_t>(vertices.size()));
    }

    DebugDrawRenderer::FrameResources* DebugDrawRenderer::ensure_capacity(
        const std::uint32_t frame_slot,
        const std::size_t vertex_count) {
        FrameResources& resources = m_frame_resources.at(frame_slot);
        if(resources.vertex_capacity >= vertex_count) {
            resources.failed_growth_capacity = 0;
            resources.growth_retry_count = 0;
            return &resources;
        }
        if(resources.failed_growth_capacity == vertex_count) {
            constexpr std::uint32_t RETRY_INTERVAL = 120;
            if(++resources.growth_retry_count < RETRY_INTERVAL) {
                return nullptr;
            }
            resources.growth_retry_count = 0;
        }

        std::size_t capacity = std::max(
            resources.vertex_capacity, INITIAL_VERTEX_CAPACITY);
        while(capacity < vertex_count) {
            if(capacity > std::numeric_limits<std::size_t>::max() / 2) {
                capacity = vertex_count;
                break;
            }
            capacity *= 2;
        }
        if(capacity > std::numeric_limits<std::size_t>::max()
                / sizeof(DebugLineVertex)) {
            LOG_ERROR("Debug draw vertex buffer size overflow");
            resources.failed_growth_capacity = vertex_count;
            resources.growth_retry_count = 0;
            return nullptr;
        }
        auto candidate = Buffer::try_create_cpu_buffer(
            m_device,
            Flags<BufferUsage>(BufferUsage::Vertex),
            capacity * sizeof(DebugLineVertex),
            true,
            nullptr,
            "debug line vertex buffer");
        if(!candidate) {
            LOG_ERROR(
                "Failed to grow debug line vertex buffer to {} vertices: {}",
                capacity,
                vk::to_string(candidate.result()));
            resources.failed_growth_capacity = vertex_count;
            resources.growth_retry_count = 0;
            return nullptr;
        }
        resources.vertex_buffer = std::move(candidate).value();
        resources.vertex_capacity = capacity;
        resources.failed_growth_capacity = 0;
        resources.growth_retry_count = 0;
        return &resources;
    }
}
