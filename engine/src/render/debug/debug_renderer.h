#pragma once

#include "common/export.h"
#include "graphics/enums.h"
#include "graphics/result.h"
#include "render/debug/line_draw_list.h"
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
    class DescriptorSet;
    class DescriptorSetLayout;
    struct Irradiance;
    struct ResolvedIrradianceVolume;
    enum class IrradianceDebugMode : uint32_t;

    // 在调用方已开启的场景 pass 内绘制；调用前须等待当前 slot 并设置 viewport/scissor。
    class COMET_API DebugRenderer {
    public:
        static Result<std::unique_ptr<DebugRenderer>, GraphicsError> create(Device& device,
            PipelineManager& pipeline_manager, uint32_t frame_slot_count, SampleCount sample_count,
            const std::shared_ptr<DescriptorSetLayout>& view_layout = {});

        Result<void, GraphicsError> render(FrameScheduler& frame_scheduler,
            const ViewProjectMatrix& view_project, const LineDrawList& draw_list);
        Result<void, GraphicsError> render_irradiance(FrameScheduler& frames,
            const DescriptorSet& view, const std::shared_ptr<Irradiance>& irradiance,
            Math::Vec2u target_size, IrradianceDebugMode mode,
            const ResolvedIrradianceVolume* realtime);

    private:
        DebugRenderer(Device& device, PipelineManager& pipelines,
            std::shared_ptr<Pipeline> pipeline, uint32_t frame_slot_count, SampleCount samples,
            std::shared_ptr<DescriptorSetLayout> view_layout);
        static Result<std::shared_ptr<Pipeline>, GraphicsError> create_pipeline(
            Device& device, PipelineManager& pipeline_manager, SampleCount sample_count);
        Result<void, GraphicsError> prepare_irradiance_pipeline();

        struct FrameResources {
            std::shared_ptr<CPUBuffer> vertex_buffer;
            uint32_t growth_retry_requests = 0;
        };

        [[nodiscard]] Result<bool, GraphicsError> ensure_capacity(
            FrameResources& resources, std::size_t vertex_count);

        Device& m_device;
        PipelineManager& m_pipelines;
        SampleCount m_samples;
        std::shared_ptr<Pipeline> m_pipeline;
        std::shared_ptr<DescriptorSetLayout> m_view_layout;
        std::shared_ptr<Pipeline> m_irradiance_pipeline;
        std::vector<FrameResources> m_frame_resources;
    };
}
