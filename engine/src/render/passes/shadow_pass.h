#pragma once

#include "graphics/queue.h"
#include "render/scene/render_submission.h"
#include "render/scene/render_geometry.h"
#include "render/resource/instance_buffer.h"

namespace Comet {
    class FrameScheduler;
    class ImageView;
    class Pipeline;
    class RenderPass;
    class RenderTarget;

    // 单方向光深度通道；每个飞行帧槽位拥有独立深度图。
    class COMET_API ShadowPass {
    public:
        struct Statistics {
            uint32_t draw_calls = 0;
            uint32_t drawn_instances = 0;
            uint64_t instance_upload_bytes = 0;
        };
        static constexpr uint32_t RESOLUTION = 1024;
        static Result<std::unique_ptr<ShadowPass>, GraphicsError> create(
            Device& device, uint32_t frame_slots);
        ShadowPass(const ShadowPass&) = delete;
        ShadowPass& operator=(const ShadowPass&) = delete;
        [[nodiscard]] static LightingData prepare(
            const RenderSubmission& submission, const RenderGeometry& geometry);
        [[nodiscard]] std::shared_ptr<ImageView> get_depth_view(uint32_t slot) const;
        [[nodiscard]] Statistics get_statistics() const { return m_statistics; }
        void reset_statistics() { m_statistics = {}; }
        [[nodiscard]] Result<std::vector<QueueSemaphoreSubmit>, GraphicsError> render(
            FrameScheduler& frames, const LightingData& lighting, const RenderGeometry& geometry);

    private:
        ShadowPass(Device& device, uint32_t frame_slots, std::shared_ptr<RenderPass> pass,
            std::shared_ptr<RenderTarget> target, std::shared_ptr<Pipeline> pipeline);
        Device& m_device;
        uint32_t m_frame_slots;
        std::shared_ptr<RenderPass> m_render_pass;
        std::shared_ptr<RenderTarget> m_target;
        std::shared_ptr<Pipeline> m_pipeline;
        std::vector<InstanceBuffer> m_instances;
        std::vector<const ResolvedRenderItem*> m_draw_queue;
        std::vector<Math::Mat4> m_transforms;
        Statistics m_statistics;
    };
}
