#pragma once

#include "graphics/result.h"
#include "render/lighting.h"

#include <optional>
#include <vector>

namespace Comet {
    class Device;
    class FrameScheduler;
    struct RayScene;
    struct Irradiance;

    // 每个世界一份探针历史，各视图只采样；不依赖 Editor 或离线烘焙。
    class COMET_API RealtimeIrradiancePass {
    public:
        struct GpuTiming {
            uint64_t serial;
            double milliseconds;
        };
        struct Statistics {
            uint32_t probes = 0;
            uint32_t updated_probes = 0;
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
        [[nodiscard]] static Result<Math::Vec3u> validate_region(
            const RenderIrradianceVolume& volume);
        [[nodiscard]] Result<std::shared_ptr<Irradiance>, GraphicsError> update(
            FrameScheduler& frames, uint64_t scene_lifetime,
            const std::shared_ptr<const RayScene>& scene, const RenderIrradianceVolume& volume,
            bool measure = false);
        [[nodiscard]] Statistics statistics() const { return m_statistics; }
        void reset();

    private:
        struct Program;
        struct History;
        struct Resources;
        struct Timing;
        [[nodiscard]] Result<std::shared_ptr<Timing>, GraphicsError> prepare_timing(
            FrameScheduler& frames);
        [[nodiscard]] Result<void, GraphicsError> initialize();
        [[nodiscard]] Result<std::shared_ptr<History>, GraphicsError> create_history(
            uint64_t scene_lifetime, const RenderIrradianceVolume& volume, Math::Vec3u dimensions);
        [[nodiscard]] Result<void, GraphicsError> prepare(uint64_t scene_lifetime,
            const std::shared_ptr<const RayScene>& scene, const RenderIrradianceVolume& volume);
        Device& m_device;
        std::shared_ptr<Program> m_program;
        std::shared_ptr<Resources> m_resources;
        Statistics m_statistics;
        uint64_t m_recorded_serial = 0;
        std::vector<std::shared_ptr<Timing>> m_timings;
    };
}
