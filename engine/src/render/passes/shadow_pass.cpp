#include "render/passes/shadow_pass.h"

namespace Comet {
    Result<std::unique_ptr<ShadowPass>, GraphicsError> ShadowPass::create(
        Device& device, uint32_t frame_slots) {
        using Creation = Result<std::unique_ptr<ShadowPass>, GraphicsError>;
        auto depth = DepthPass::create(device, frame_slots,
            {RESOLUTION / 2 * LightingData::SHADOW_ATLAS_COLUMNS,
                RESOLUTION / 2 * LightingData::SHADOW_ATLAS_ROWS},
            LightingData::MAX_SHADOW_VIEWS);
        if(!depth)
            return Creation::failure(depth.error());
        return Creation::success(
            std::unique_ptr<ShadowPass>(new ShadowPass(std::move(depth).value())));
    }

    LightingData ShadowPass::prepare(
        const RenderSubmission& submission, const RenderGeometry& geometry) {
        auto lighting = LightingData::prepare(submission.world.lights);
        if(geometry.get_scene_bounds() && submission.view_project_matrix) {
            const auto& camera = *submission.view_project_matrix;
            lighting.prepare_shadow(
                *geometry.get_scene_bounds(), RESOLUTION, camera.view, camera.projection);
        }
        return lighting;
    }

    Result<std::vector<QueueSemaphoreSubmit>, GraphicsError> ShadowPass::render(
        FrameScheduler& frames, const LightingData& lighting, const RenderGeometry& geometry) {
        std::array<DepthPass::View, LightingData::MAX_SHADOW_VIEWS> views;
        for(size_t i = 0; i < views.size(); ++i) {
            const auto& shadow = lighting.shadows[i];
            views[i] = {shadow.view_projection, shadow.atlas_rect, shadow.light_index >= 0};
        }
        auto result = m_depth->render(frames, views, geometry);
        static_cast<DepthPass::Statistics&>(m_statistics) = m_depth->get_statistics();
        m_statistics.cascades = uint32_t(lighting.shadow_cascade_count);
        m_statistics.spot_lights = uint32_t(lighting.spot_shadow_count);
        m_statistics.excess_spot_lights = uint32_t(lighting.excess_spot_shadows);
        m_statistics.point_lights = uint32_t(lighting.point_shadow_count);
        m_statistics.excess_point_lights = uint32_t(lighting.excess_point_shadows);
        return result;
    }
}
