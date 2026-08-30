#pragma once

#include "graphics/enums.h"
#include "render/debug/debug_draw.h"
#include "render/scene/render_types.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace Comet {
    class CommandBuffer;
    class CPUBuffer;
    class Device;
    class Pipeline;
    class PipelineManager;
    class ResourceManager;

    class DebugDrawRenderer {
    public:
        DebugDrawRenderer(
            Device& device,
            PipelineManager& pipeline_manager,
            ResourceManager& resource_manager,
            std::uint32_t frame_slot_count,
            SampleCount sample_count);

        void render(
            const CommandBuffer& command_buffer,
            std::uint32_t frame_slot,
            const ViewProjectMatrix& view_project,
            const DebugDrawList& draw_list);

    private:
        struct FrameResources {
            std::shared_ptr<CPUBuffer> vertex_buffer;
            std::size_t vertex_capacity = 0;
            std::size_t failed_growth_capacity = 0;
            std::uint32_t growth_retry_count = 0;
        };

        [[nodiscard]] FrameResources* ensure_capacity(
            std::uint32_t frame_slot,
            std::size_t vertex_count);

        Device& m_device;
        std::shared_ptr<Pipeline> m_pipeline;
        std::vector<FrameResources> m_frame_resources;
    };
}
