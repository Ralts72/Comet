#pragma once

#include "render/passes/depth_pass.h"

namespace Comet {
    // 每个 View 的方向光级联与局部光图集；几何绘制交给 DepthPass。
    class COMET_API ShadowPass {
    public:
        struct Statistics: DepthPass::Statistics {
            uint32_t cascades = 0;
            uint32_t spot_lights = 0;
            uint32_t excess_spot_lights = 0;
            uint32_t point_lights = 0;
            uint32_t excess_point_lights = 0;
            uint32_t rendered_passes = 0;
            uint32_t skipped_passes = 0;
            uint64_t atlas_texels = 0;
        };
        static constexpr uint32_t RESOLUTION = 1024;
        static Result<std::unique_ptr<ShadowPass>, GraphicsError> create(
            Device& device, uint32_t frame_slots);
        [[nodiscard]] static LightingData prepare(
            const RenderSubmission& submission, const RenderGeometry& geometry);
        [[nodiscard]] std::shared_ptr<ImageView> get_depth_view(uint32_t slot) const {
            return m_depth ? m_depth->get_depth_view(slot) : m_fallback.at(slot).view;
        }
        [[nodiscard]] static bool needs_shadow(const LightingData& lighting);
        [[nodiscard]] Result<void, GraphicsError> prepare_frame(
            FrameScheduler& frames, const LightingData& lighting);
        [[nodiscard]] Statistics get_statistics() const { return m_statistics; }
        void reset_statistics() { m_statistics = {}; }
        [[nodiscard]] Result<std::vector<QueueSemaphoreSubmit>, GraphicsError> render(
            FrameScheduler& frames, const LightingData& lighting, const RenderGeometry& geometry);

    private:
        ShadowPass(Device& device, uint32_t frame_slots)
            : m_device(device), m_frame_slots(frame_slots) {}
        Device& m_device;
        uint32_t m_frame_slots;
        struct Fallback {
            std::shared_ptr<ImageView> view;
            uint64_t recorded_serial = 0;
            bool initialized = false;
        };
        std::vector<Fallback> m_fallback;
        std::unique_ptr<DepthPass> m_depth;
        Statistics m_statistics;
    };
}
