#pragma once

#include "render/scene/render_submission.h"
#include "graphics/queue.h"

namespace Comet {
    class Device;
    class FrameScheduler;
    class ImageView;
    class Pipeline;
    class RenderPass;
    class RenderTarget;

    // 一张方向光深度图/slot；CPU 投影与 GPU pass 分离，不读取 Scene。
    class COMET_API ShadowRenderer {
    public:
        static constexpr uint32_t RESOLUTION = 1024;
        ShadowRenderer(Device& device, uint32_t frame_slots);
        [[nodiscard]] std::shared_ptr<ImageView> get_depth_view(uint32_t slot) const;
        [[nodiscard]] static LightingData prepare(const RenderSubmission& submission);
        [[nodiscard]] std::vector<QueueSemaphoreSubmit> render(FrameScheduler& frames,
            const LightingData& lighting, std::span<const ResolvedRenderItem> items);

    private:
        std::shared_ptr<RenderPass> m_render_pass;
        std::shared_ptr<RenderTarget> m_target;
        std::shared_ptr<Pipeline> m_pipeline;
    };
}
