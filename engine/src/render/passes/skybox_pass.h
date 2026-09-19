#pragma once

#include "common/export.h"
#include "graphics/enums.h"
#include "graphics/result.h"
#include "graphics/queue.h"

#include <memory>
#include <vector>

namespace Comet {
    class Device;
    class FrameScheduler;
    class PipelineManager;
    class Pipeline;
    class DescriptorSetLayout;
    class Sampler;
    class Texture;
    struct RenderSubmission;
    struct SampledImageBinding;

    // Records the background inside the scene render pass, before opaque geometry.
    class COMET_API SkyboxPass {
    public:
        static Result<std::unique_ptr<SkyboxPass>, GraphicsError> create(
            Device& device, PipelineManager& pipelines, SampleCount samples, uint32_t frame_slots);
        [[nodiscard]] Result<std::vector<QueueSemaphoreSubmit>, GraphicsError> render(
            FrameScheduler& frames, const RenderSubmission& submission);

    private:
        explicit SkyboxPass(Device& device) : m_device(device) {}

        Device& m_device;
        std::shared_ptr<DescriptorSetLayout> m_layout;
        std::shared_ptr<Sampler> m_sampler;
        std::shared_ptr<Pipeline> m_pipeline;
        std::vector<std::shared_ptr<SampledImageBinding>> m_bindings;
    };
}
