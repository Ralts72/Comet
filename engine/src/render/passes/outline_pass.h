#pragma once

#include "render/render_graph.h"
#include "render/passes/depth_pass.h"

namespace Comet {
    class Pipeline;
    class FrameBuffer;
    struct SampledImageBinding;

    // 复用几何深度的蒙皮、裁切和绕序规则；遮罩与合成由所属 View 的图调度。
    class COMET_API OutlinePass {
    public:
        struct Resources {
            Math::Vec2u size;
            std::vector<std::shared_ptr<ImageView>> images;
            std::vector<std::shared_ptr<FrameBuffer>> framebuffers;
            std::vector<std::shared_ptr<SampledImageBinding>> bindings;
        };
        static Result<std::unique_ptr<OutlinePass>, GraphicsError> create(
            Device& device, uint32_t slots, Math::Vec2u size);
        Result<std::shared_ptr<Resources>, GraphicsError> create_resources(
            const RenderGraph::Resources&, RenderGraph::ResourceId output, Math::Vec2u size) const;
        std::array<std::shared_ptr<ImageView>, 3> mask_images(uint32_t slot) const;
        Result<std::vector<QueueSemaphoreSubmit>, GraphicsError> mask(
            FrameScheduler&, const ViewProjectMatrix&, const RenderGeometry&);
        Result<void, GraphicsError> composite(FrameScheduler&, const std::shared_ptr<Resources>&,
            const std::shared_ptr<ImageView>& scene, const std::shared_ptr<ImageView>& depth,
            Math::Vec2u output_size);

    private:
        OutlinePass(Device& device, uint32_t slots) : m_device(device), m_slots(slots) {}
        Device& m_device;
        uint32_t m_slots;
        float m_width = 0;
        std::unique_ptr<DepthPass> m_mask;
        std::shared_ptr<RenderPass> m_pass;
        std::shared_ptr<Pipeline> m_pipeline;
        std::shared_ptr<DescriptorSetLayout> m_layout;
        std::shared_ptr<Sampler> m_sampler;
    };
}
