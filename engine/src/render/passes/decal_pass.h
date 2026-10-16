#pragma once

#include "graphics/queue.h"
#include "graphics/pipeline/descriptor_set.h"
#include "render/scene/render_submission.h"
#include <unordered_map>

namespace Comet {
    class FrameScheduler;
    class PipelineManager;
    class Pipeline;
    class Sampler;
    struct SampledImageBinding;

    // 在不透明绘制与透明合成之间投影，借用已有视图光照绑定。
    class COMET_API DecalPass {
    public:
        static Result<std::unique_ptr<DecalPass>, GraphicsError> create(Device& device,
            PipelineManager& pipelines, std::shared_ptr<Texture> white,
            std::shared_ptr<DescriptorSetLayout> view_layout, uint32_t frame_slots,
            SampleCount samples);
        Result<std::vector<QueueSemaphoreSubmit>, GraphicsError> render(FrameScheduler& frames,
            const RenderSubmission& submission, const DescriptorSet& view,
            const std::shared_ptr<ImageView>& depth, Math::Vec2u size);

    private:
        explicit DecalPass(Device& device) : m_device(device) {}
        Device& m_device;
        std::shared_ptr<Pipeline> m_pipeline;
        std::shared_ptr<DescriptorSetLayout> m_layout;
        std::shared_ptr<Sampler> m_color_sampler, m_depth_sampler;
        std::shared_ptr<Texture> m_white;
        struct Binding {
            std::shared_ptr<SampledImageBinding> images;
            bool used = false;
        };
        std::vector<std::unordered_map<const Texture*, Binding>> m_bindings;
        std::vector<const ResolvedDecal*> m_visible;
        uint32_t m_excess = 0;
    };
}
