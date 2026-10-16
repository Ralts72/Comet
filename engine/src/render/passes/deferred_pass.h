#pragma once

#include "common/export.h"
#include "graphics/result.h"
#include "graphics/pipeline/descriptor_set.h"
#include "graphics/queue.h"
#include "core/math_utils.h"

#include <memory>
#include <span>
#include <vector>

namespace Comet {
    class Device;
    class RenderPass;
    class PipelineManager;
    class Pipeline;
    class FrameScheduler;
    class MaterialRenderer;
    class RenderTarget;
    class ImageView;
    struct SampledImageBinding;
    class Sampler;

    // 不透明表面先写 G-buffer；光照在 HDR 场景 pass 中还原颜色及深度，随后继续 Forward。
    class COMET_API DeferredPass {
    public:
        static constexpr uint32_t COLOR_COUNT = 4;
        struct Resources {
            std::shared_ptr<RenderTarget> target;
            std::vector<std::shared_ptr<SampledImageBinding>> bindings;
        };
        [[nodiscard]] static Result<std::unique_ptr<DeferredPass>, GraphicsError> create(
            Device& device, uint32_t frame_slots);
        ~DeferredPass();
        [[nodiscard]] PipelineManager& pipelines() const { return *m_pipelines; }
        [[nodiscard]] Result<void, GraphicsError> create_lighting(
            PipelineManager& pipelines, const std::shared_ptr<DescriptorSetLayout>& view_layout);
        [[nodiscard]] Result<std::shared_ptr<Resources>, GraphicsError> create_resources(
            Math::Vec2u size) const;
        [[nodiscard]] static std::span<const std::shared_ptr<ImageView>> images(
            const Resources& resources, uint32_t slot);
        void record_geometry(FrameScheduler& frames, const std::shared_ptr<Resources>& resources,
            MaterialRenderer& materials, std::vector<QueueSemaphoreSubmit>& waits);
        void record_lighting(FrameScheduler& frames, const std::shared_ptr<Resources>& resources,
            const DescriptorSet& view);

    private:
        DeferredPass(Device& device, uint32_t slots) : m_device(device), m_frame_slots(slots) {}
        Device& m_device;
        uint32_t m_frame_slots;
        std::shared_ptr<RenderPass> m_pass;
        std::unique_ptr<PipelineManager> m_pipelines;
        std::shared_ptr<DescriptorSetLayout> m_layout;
        std::shared_ptr<Sampler> m_sampler;
        std::shared_ptr<Pipeline> m_lighting;
    };
}
