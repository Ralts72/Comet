#pragma once

#include "common/export.h"
#include "graphics/enums.h"
#include "graphics/result.h"

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

    // Records the background inside the scene render pass, before opaque geometry.
    class COMET_API SkyboxPass {
    public:
        static Result<std::unique_ptr<SkyboxPass>, GraphicsError> create(
            Device& device, PipelineManager& pipelines, SampleCount samples, uint32_t frame_slots);
        [[nodiscard]] Result<void, GraphicsError> render(
            FrameScheduler& frames, const RenderSubmission& submission);

    private:
        struct Binding;
        explicit SkyboxPass(Device& device) : m_device(device) {}
        Result<std::shared_ptr<Binding>, GraphicsError> create_binding(
            const std::shared_ptr<Texture>& texture);

        Device& m_device;
        std::shared_ptr<DescriptorSetLayout> m_layout;
        std::shared_ptr<Sampler> m_sampler;
        std::shared_ptr<Pipeline> m_pipeline;
        std::vector<std::shared_ptr<Binding>> m_bindings;
    };
}
