#pragma once

#include "graphics/result.h"
#include "render/lighting.h"

#include <optional>
#include <vector>

namespace Comet {
    class Device;
    class FrameScheduler;
    struct RayScene;
    struct ResolvedIrradianceVolume;
    struct RaySceneBuffers;

    // 一个世界共用追踪输入，各区域独立历史；各视图只采样。
    class COMET_API RealtimeIrradiancePass {
    public:
        struct GpuTiming {
            uint64_t serial;
            double milliseconds;
        };
        struct Statistics {
            uint32_t probes = 0;
            uint32_t updated_probes = 0;
            uint32_t regions = 0;
            uint32_t evaluated_probes = 0;
            uint32_t rays = 0;
            uint64_t updates = 0;
            uint64_t image_bytes = 0;
            uint64_t upload_bytes = 0;
            uint64_t geometry_upload_bytes = 0;
            std::optional<GpuTiming> gpu;
        };

        explicit RealtimeIrradiancePass(Device& device);
        ~RealtimeIrradiancePass();
        [[nodiscard]] static Result<void, GraphicsError> check_support(const Device& device);
        [[nodiscard]] Result<std::vector<ResolvedIrradianceVolume>, GraphicsError> update(
            FrameScheduler& frames, uint64_t scene_lifetime,
            const std::shared_ptr<const RayScene>& scene,
            std::span<const RenderIrradianceVolume> volumes, uint32_t total_budget,
            bool measure = false);
        [[nodiscard]] Statistics statistics() const { return m_statistics; }
        void reset();

    private:
        struct Program;
        struct History;
        struct Resources;
        struct Timing;
        struct Feedback;
        Result<std::shared_ptr<Feedback>, GraphicsError> prepare_feedback(
            FrameScheduler&, std::span<const RenderIrradianceVolume>);
        [[nodiscard]] Result<std::shared_ptr<Timing>, GraphicsError> prepare_timing(
            FrameScheduler& frames);
        [[nodiscard]] Result<void, GraphicsError> initialize();
        [[nodiscard]] Result<std::shared_ptr<History>, GraphicsError> create_history(
            const RenderIrradianceVolume& volume, Math::Vec3u dimensions);
        [[nodiscard]] Result<void, GraphicsError> prepare(uint64_t scene_lifetime,
            const std::shared_ptr<const RayScene>& scene,
            std::span<const RenderIrradianceVolume> volumes);
        [[nodiscard]] Result<void, GraphicsError> record_regions(FrameScheduler& frames,
            std::span<const RenderIrradianceVolume> volumes, std::span<const uint32_t> grants,
            bool geometry_moved, std::span<ResolvedIrradianceVolume> outputs);
        Device& m_device;
        std::shared_ptr<Program> m_program;
        std::vector<std::shared_ptr<Resources>> m_regions;
        std::shared_ptr<RaySceneBuffers> m_buffers;
        uint64_t m_scene_lifetime = 0;
        uint32_t m_budget_cursor = 0;
        Statistics m_statistics;
        uint64_t m_recorded_serial = 0;
        std::vector<std::shared_ptr<Timing>> m_timings;
        std::vector<std::shared_ptr<Feedback>> m_feedback;
    };
}
