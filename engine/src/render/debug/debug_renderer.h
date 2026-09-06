#pragma once

#include "graphics/enums.h"
#include "render/line_draw_list.h"
#include "render/scene/render_types.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace Comet {
    class CPUBuffer;
    class Device;
    class FrameScheduler;
    class Pipeline;
    class PipelineManager;
    class ResourceManager;

    // 在调用方已开启的场景 pass 内绘制；调用前须等待当前 slot 并设置 viewport/scissor。
    class DebugRenderer {
    public:
        DebugRenderer(Device& device, PipelineManager& pipeline_manager,
            ResourceManager& resource_manager, uint32_t frame_slot_count,
            SampleCount sample_count);

        void render(FrameScheduler& frame_scheduler,
            const ViewProjectMatrix& view_project, const LineDrawList& draw_list);

    private:
        struct FrameResources {
            std::shared_ptr<CPUBuffer> vertex_buffer;
            uint32_t growth_retry_requests = 0;
        };

        [[nodiscard]] bool ensure_capacity(
            FrameResources& resources, std::size_t vertex_count);

        Device& m_device;
        std::shared_ptr<Pipeline> m_pipeline;
        std::vector<FrameResources> m_frame_resources;
    };
}
