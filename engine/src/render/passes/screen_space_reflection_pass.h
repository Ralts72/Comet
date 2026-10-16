#pragma once

#include "render/render_graph.h"
#include "graphics/pipeline/descriptor_set.h"
#include "scene/scene_settings.h"

namespace Comet {
    class Device;
    class Pipeline;
    class RenderPass;
    class ImageView;
    class FrameBuffer;
    class Sampler;
    struct SampledImageBinding;

    // 单采样 HDR 的屏幕空间镜面替换；无效命中保持原探针／IBL。
    class COMET_API ScreenSpaceReflectionPass {
    public:
        struct Resources {
            Math::Vec2u size;
            std::vector<std::shared_ptr<ImageView>> images;
            std::vector<std::shared_ptr<FrameBuffer>> framebuffers;
            std::vector<std::shared_ptr<SampledImageBinding>> bindings;
        };
        [[nodiscard]] static Result<std::unique_ptr<ScreenSpaceReflectionPass>, GraphicsError>
        create(Device& device, uint32_t slots,
            const std::shared_ptr<DescriptorSetLayout>& view_layout);
        [[nodiscard]] Result<std::shared_ptr<Resources>, GraphicsError> create_resources(
            const RenderGraph::Resources& resources, RenderGraph::ResourceId output,
            Math::Vec2u size) const;
        [[nodiscard]] static std::shared_ptr<ImageView> output(
            const Resources& resources, uint32_t slot);
        [[nodiscard]] Result<void, GraphicsError> render(FrameScheduler& frames,
            const std::shared_ptr<Resources>& resources, const DescriptorSet& view,
            std::span<const std::shared_ptr<ImageView>> gbuffer,
            const std::shared_ptr<ImageView>& hdr, const std::shared_ptr<ImageView>& depth,
            const ScreenSpaceReflectionSettings& settings);

    private:
        ScreenSpaceReflectionPass(Device& device, uint32_t slots)
            : m_device(device), m_slots(slots) {}
        Device& m_device;
        uint32_t m_slots;
        std::shared_ptr<RenderPass> m_pass;
        std::shared_ptr<Pipeline> m_pipeline;
        std::shared_ptr<DescriptorSetLayout> m_layout;
        std::shared_ptr<Sampler> m_nearest, m_linear;
    };
}
