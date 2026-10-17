#pragma once

#include "graphics/result.h"
#include "core/math_utils.h"
#include <memory>
#include <vector>
#include <array>

namespace Comet {
    class Device;
    class FrameScheduler;
    class PipelineManager;
    class RenderPass;
    class RenderTarget;
    class Pipeline;
    class Sampler;
    class ImageView;
    class DescriptorSetLayout;
    class DescriptorSet;
    struct SampledImageBinding;

    // 加权颜色和对数透射率共用加法混合；无需独立 MRT 混合特性。
    class COMET_API TransparencyPass {
    public:
        struct Resources {
            std::shared_ptr<RenderTarget> accumulation, output;
            std::vector<std::shared_ptr<SampledImageBinding>> depth, composite;
        };
        ~TransparencyPass();
        static Result<std::unique_ptr<TransparencyPass>, GraphicsError> create(
            Device&, uint32_t slots);
        Result<std::shared_ptr<Resources>, GraphicsError> create_resources(Math::Vec2u) const;
        PipelineManager& pipelines() const;
        const std::shared_ptr<DescriptorSetLayout>& depth_layout() const { return m_depth_layout; }
        Result<DescriptorSet, GraphicsError> begin(FrameScheduler&,
            const std::shared_ptr<Resources>&, const std::shared_ptr<ImageView>& depth);
        Result<void, GraphicsError> composite(FrameScheduler&, const std::shared_ptr<Resources>&,
            const std::shared_ptr<ImageView>& scene);
        static std::array<std::shared_ptr<ImageView>, 3> images(const Resources&, uint32_t slot);

    private:
        TransparencyPass(Device&, uint32_t);
        Device& m_device;
        uint32_t m_slots;
        std::shared_ptr<RenderPass> m_accumulation_pass, m_composite_pass;
        std::unique_ptr<PipelineManager> m_pipelines;
        std::shared_ptr<DescriptorSetLayout> m_depth_layout, m_composite_layout;
        std::shared_ptr<Sampler> m_sampler;
        std::shared_ptr<Pipeline> m_composite;
    };
}
